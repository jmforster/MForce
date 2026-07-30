"""Reusable figure-transform library (comp lane, Matt 2026-07-30).

A family of operations that take an atomic melodic figure and produce a
musically-related VARIANT — the building blocks for non-literal figure repeats
(transposed and/or modified repeats instead of verbatim ones).

Figure schema (matches markov_phrase.py / bake_off.py exactly):
    fig = {"units": [{"duration": float, "step": int}, ...]}
where `step` is a SCALE-STEP delta from the previous note and units[0]["step"]
is ALWAYS 0 (the figure is pitch-anchored elsewhere, via phrase connectors).

Contract for every transform in this module:
  * signature  f(fig, ...) -> fig
  * preserves  units[0]["step"] == 0   (the anchoring invariant)
  * returns a NEW dict; NEVER mutates its input (markov_phrase relies on
    figures never being mutated in place).

Run the self-test / demo from repo root:
    python corpus/mtd_seg/figure_transforms.py
"""
import pathlib
import sys

HERE = pathlib.Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))


# --------------------------------------------------------------------------- #
# helpers
# --------------------------------------------------------------------------- #
def net_step(fig):
    """Net scale-step displacement of the whole figure (sum of step deltas).
    Equals the cumulative degree of the last note relative to the first."""
    return sum(u["step"] for u in fig["units"])


def _degrees(units):
    """Cumulative scale-degree of each note, deg[0] == 0 (unit0 step is 0)."""
    deg, c = [], 0
    for u in units:
        c += u["step"]
        deg.append(c)
    return deg


def _from_degrees(durations, degrees):
    """Rebuild a figure dict from durations + cumulative degrees, re-anchoring
    so degrees[0] maps to step 0 (keeps the anchoring invariant)."""
    n = len(degrees)
    base = degrees[0]
    d = [x - base for x in degrees]                  # re-anchor: d[0] == 0
    steps = [0] + [d[i] - d[i - 1] for i in range(1, n)]
    return {"units": [{"duration": float(durations[i]), "step": int(steps[i])}
                      for i in range(n)]}


# --------------------------------------------------------------------------- #
# pitch-contour transforms
# --------------------------------------------------------------------------- #
def invert(fig):
    """Melodic INVERSION: mirror the scale-step contour about the first note
    (every ascending step becomes the same-size descending step and vice
    versa). step0 stays 0 (mirror of 0 is 0). Matches markov_phrase.invert."""
    return {"units": [{"duration": u["duration"], "step": -u["step"]}
                      for u in fig["units"]]}


def retrograde(fig):
    """RETROGRADE: play the figure's notes back-to-front (durations and pitches
    reversed), re-anchored so step0 == 0. Matches markov_phrase.retrograde."""
    units = fig["units"]
    n = len(units)
    durs = [u["duration"] for u in units]
    deg = _degrees(units)
    return _from_degrees(durs[::-1], deg[::-1])


def _scale_step(s, factor):
    """Scale one step delta by `factor`, rounding to int but never collapsing a
    non-zero step to 0 (a note that moved keeps moving, min magnitude 1, sign
    preserved). Zero stays zero."""
    if s == 0:
        return 0
    v = int(round(s * factor))
    if v == 0:
        v = 1 if s > 0 else -1
    return v


def expand_intervals(fig, factor=2.0):
    """INTERVAL EXPANSION: widen the melodic reach by scaling every non-zero
    scale-step by `factor` (contour sign preserved, min magnitude 1). Same
    rhythm and same up/down shape, but the leaps get bigger. step0 stays 0."""
    return {"units": [{"duration": u["duration"],
                       "step": _scale_step(u["step"], factor)}
                      for u in fig["units"]]}


def compress_intervals(fig, factor=2.0):
    """INTERVAL COMPRESSION: narrow the melodic reach by dividing every non-zero
    scale-step by `factor` (contour sign preserved, min magnitude 1 so a step
    never collapses to a repeated note). Same rhythm and shape. step0 stays 0."""
    return {"units": [{"duration": u["duration"],
                       "step": _scale_step(u["step"], 1.0 / factor)}
                      for u in fig["units"]]}


def rotate(fig, k=1):
    """ROTATION: cyclic permutation of the motif's (duration, step) notes by k
    positions, then re-anchor so step0 == 0. The same set of note-to-note moves
    is heard starting from a different point in the cycle."""
    units = fig["units"]
    n = len(units)
    if n == 0:
        return {"units": []}
    k %= n
    pairs = [(u["duration"], u["step"]) for u in units]
    rot = pairs[k:] + pairs[:k]
    out = [{"duration": float(d), "step": int(s)} for d, s in rot]
    out[0]["step"] = 0                               # re-anchor
    return {"units": out}


# --------------------------------------------------------------------------- #
# rhythmic transforms
# --------------------------------------------------------------------------- #
def augment(fig, factor=2.0):
    """RHYTHMIC AUGMENTATION: stretch the figure in time by multiplying every
    duration by `factor` (pitches untouched). Total duration scales by factor."""
    return {"units": [{"duration": float(u["duration"]) * factor,
                       "step": u["step"]}
                      for u in fig["units"]]}


def diminish(fig, factor=2.0):
    """RHYTHMIC DIMINUTION: compress the figure in time by dividing every
    duration by `factor` (pitches untouched). Total duration scales by 1/factor."""
    return {"units": [{"duration": float(u["duration"]) / factor,
                       "step": u["step"]}
                      for u in fig["units"]]}


# --------------------------------------------------------------------------- #
# ornamentation
# --------------------------------------------------------------------------- #
def ornament(fig, rng):
    """ORNAMENTATION (passing/neighbor tone): split the longest interior note
    into two halves of equal duration, inserting a stepwise (+-1) intermediate
    pitch that then resolves back onto the original note. Total duration of the
    split note is preserved, so the figure's overall length is unchanged.

    The split target is the max-duration unit at index >= 1 (index 0 is the
    anchor and is left intact so step0 stays 0). Figures shorter than 2 notes
    are returned as an unmodified copy."""
    units = fig["units"]
    n = len(units)
    if n < 2:
        return {"units": [dict(u) for u in units]}
    # choose the longest interior note (ties -> earliest)
    i = max(range(1, n), key=lambda j: units[j]["duration"])
    d = units[i]["duration"]
    s = units[i]["step"]
    off = 1 if rng.random() < 0.5 else -1            # neighbor direction
    # ornament note reaches deg_i + off, then resolves by -off onto deg_i.
    orn = {"duration": float(d) / 2.0, "step": int(s + off)}
    resolve = {"duration": float(d) / 2.0, "step": int(-off)}
    out = [dict(u) for u in units[:i]] + [orn, resolve] + \
          [dict(u) for u in units[i + 1:]]
    return {"units": out}


# --------------------------------------------------------------------------- #
# markov resampling (delegated)
# --------------------------------------------------------------------------- #
def vary_tail(fig, model, rng):
    """VARIED REPEAT: keep the figure's head (first ceil(n/2) notes) and
    resample the tail from the Markov chain conditioned on that head. Same
    opening, fresh ending — the classic A / A' shape. Delegates to
    markov_phrase.vary_tail and exposes only the variant figure (that function
    also returns a diff-count, dropped here for a uniform f(fig,...)->fig)."""
    from markov_phrase import vary_tail as _mp_vary_tail
    variant, _n_diff = _mp_vary_tail(fig, model, rng)
    return variant


# --------------------------------------------------------------------------- #
# registry
# --------------------------------------------------------------------------- #
# name -> {fn, arity} where arity documents what the transform needs beyond the
# figure: "fig" (pure), "fig+param" (numeric factor/k), "fig+rng", "fig+model".
TRANSFORMS = {
    "invert":            {"fn": invert,            "arity": "fig"},
    "retrograde":        {"fn": retrograde,        "arity": "fig"},
    "augment":           {"fn": augment,           "arity": "fig+param"},
    "diminish":          {"fn": diminish,          "arity": "fig+param"},
    "expand_intervals":  {"fn": expand_intervals,  "arity": "fig+param"},
    "compress_intervals":{"fn": compress_intervals,"arity": "fig+param"},
    "rotate":            {"fn": rotate,            "arity": "fig+param"},
    "ornament":          {"fn": ornament,          "arity": "fig+rng"},
    "vary_tail":         {"fn": vary_tail,         "arity": "fig+model"},
}


# --------------------------------------------------------------------------- #
# demo + self-test
# --------------------------------------------------------------------------- #
def _fmt(fig):
    steps = " ".join(f"{u['step']:+d}" for u in fig["units"])
    durs = " ".join(f"{u['duration']:g}" for u in fig["units"])
    return f"steps=[{steps}]  durs=[{durs}]  net={net_step(fig):+d}"


def _reach(fig):
    """Contour reach: span of cumulative degrees (max - min)."""
    deg = _degrees(fig["units"])
    return max(deg) - min(deg)


def _total_dur(fig):
    return sum(u["duration"] for u in fig["units"])


def _steps(fig):
    return [u["step"] for u in fig["units"]]


def _durs(fig):
    return [u["duration"] for u in fig["units"]]


def main():
    import copy
    import random

    from markov_model import MarkovModel
    from markov_phrase import _sample_figure
    from bake_off import realize
    import score_generated as sg

    rng = random.Random(20260730)
    model = MarkovModel.load()
    cs = sg.corpus_stats()

    # 1. sample a handful of real Markov figures (5..8 notes for meaningful tails)
    figs = [_sample_figure(model, rng, kmin=5, kmax=8) for _ in range(6)]

    print("=" * 72)
    print("SAMPLED FIGURES")
    print("=" * 72)
    for i, f in enumerate(figs):
        print(f"  fig{i}: {_fmt(f)}")

    # 2. before/after eyeball on fig0
    demo = figs[0]
    print("\n" + "=" * 72)
    print("TRANSFORM DEMO (fig0)")
    print("=" * 72)
    print(f"  ORIGINAL           : {_fmt(demo)}")
    applied = {
        "invert":             invert(demo),
        "retrograde":         retrograde(demo),
        "augment(x2)":        augment(demo, 2),
        "diminish(x2)":       diminish(demo, 2),
        "expand_intervals(2)":expand_intervals(demo, 2),
        "compress_interval(2)":compress_intervals(demo, 2),
        "rotate(1)":          rotate(demo, 1),
        "ornament":           ornament(demo, rng),
        "vary_tail":          vary_tail(demo, model, rng),
    }
    for name, out in applied.items():
        print(f"  {name:20}: {_fmt(out)}")

    # 3. programmatic invariant asserts across ALL sampled figures
    print("\n" + "=" * 72)
    print("INVARIANT CHECKS")
    print("=" * 72)
    n_checks = 0

    for i, f in enumerate(figs):
        snapshot = copy.deepcopy(f)                  # mutation guard baseline

        outs = {
            "invert":     invert(f),
            "retrograde": retrograde(f),
            "augment":    augment(f, 2),
            "diminish":   diminish(f, 2),
            "expand":     expand_intervals(f, 2),
            "compress":   compress_intervals(f, 2),
            "rotate":     rotate(f, 1),
            "ornament":   ornament(f, random.Random(i)),
            "vary_tail":  vary_tail(f, model, random.Random(i)),
        }

        # (a) anchoring invariant: every output has units[0]["step"] == 0
        for name, out in outs.items():
            assert out["units"][0]["step"] == 0, \
                f"fig{i} {name}: step0 != 0 ({out['units'][0]['step']})"
            n_checks += 1

        # (b) augment/diminish scale total duration by the factor
        assert abs(_total_dur(outs["augment"]) - 2 * _total_dur(f)) < 1e-9, \
            f"fig{i} augment: total duration not x2"
        assert abs(_total_dur(outs["diminish"]) - _total_dur(f) / 2) < 1e-9, \
            f"fig{i} diminish: total duration not /2"
        n_checks += 2

        # (c) expand_intervals grows melodic reach (strictly, when the figure
        #     actually moves; equal only for a static figure)
        moves = any(u["step"] != 0 for u in f["units"])
        if moves:
            assert _reach(outs["expand"]) > _reach(f), \
                f"fig{i} expand: reach did not grow ({_reach(f)} -> {_reach(outs['expand'])})"
        else:
            assert _reach(outs["expand"]) >= _reach(f)
        n_checks += 1

        # (d) retrograde is an involution on the contour: R(R(x)) == x
        rr = retrograde(retrograde(f))
        assert _steps(rr) == _steps(f) and _durs(rr) == _durs(f), \
            f"fig{i} retrograde(retrograde) != identity"
        n_checks += 1

        # (e) ornament preserves total duration (split note halves sum back)
        assert abs(_total_dur(outs["ornament"]) - _total_dur(f)) < 1e-9, \
            f"fig{i} ornament: total duration changed"
        n_checks += 1

        # (f) no-mutation: the input dict is byte-for-byte unchanged
        assert f == snapshot, f"fig{i}: input figure was mutated!"
        n_checks += 1

    print(f"  ran {n_checks} assertions across {len(figs)} figures - all passed")

    # 4. plausibility: realize each transform's output and score vs corpus.
    #    Composite in [0,1]; we want transforms to add variation while the
    #    composite does NOT collapse (stays broadly corpus-plausible).
    print("\n" + "=" * 72)
    print("PLAUSIBILITY (mean composite over sampled figures, 0..1)")
    print("=" * 72)

    def composite_of(fig):
        mel = realize(_steps(fig), _durs(fig))
        return sg.score(mel, cs)["composite"]

    def variants(name, f, r):
        if name == "original":       return f
        if name == "invert":         return invert(f)
        if name == "retrograde":     return retrograde(f)
        if name == "augment":        return augment(f, 2)
        if name == "diminish":       return diminish(f, 2)
        if name == "expand_intervals":  return expand_intervals(f, 2)
        if name == "compress_intervals":return compress_intervals(f, 2)
        if name == "rotate":         return rotate(f, 1)
        if name == "ornament":       return ornament(f, r)
        if name == "vary_tail":      return vary_tail(f, model, r)

    order = ["original", "invert", "retrograde", "augment", "diminish",
             "expand_intervals", "compress_intervals", "rotate",
             "ornament", "vary_tail"]
    print(f"  {'transform':20} {'mean_composite':>15}")
    for name in order:
        comps = [composite_of(variants(name, f, random.Random(1000 + i)))
                 for i, f in enumerate(figs)]
        print(f"  {name:20} {sum(comps) / len(comps):>15.3f}")

    print("\nALL INVARIANTS OK")


if __name__ == "__main__":
    main()
