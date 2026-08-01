"""Unit tests (plain asserts, no pytest) for markov_phrase Tasks 1-3
plus the per-occurrence transform wiring (backlog #10)."""
import random

from markov_phrase import (net_step, invert, retrograde,
                           build_combination, PATTERNS, make_template,
                           _tokens_v2, make_variant, build_phrase_v3,
                           INDEPENDENT_OPS, INVOLUTION_OPS, MIXED_MENU,
                           V3_PATTERNS)

A = {"units": [{"duration":0.5,"step":0},{"duration":0.5,"step":1},
               {"duration":0.5,"step":1},{"duration":0.5,"step":-1}]}
B = {"units": [{"duration":1.0,"step":0},{"duration":1.0,"step":2}]}


def test_net_step():
    assert net_step(A) == 1

def test_invert():
    inv = invert(A)
    assert [u["step"] for u in inv["units"]] == [0,-1,-1,1]
    assert [u["duration"] for u in inv["units"]] == [0.5,0.5,0.5,0.5]
    assert net_step(inv) == -1

def test_retrograde():
    r = retrograde(A)
    assert [u["step"] for u in r["units"]] == [0,1,-1,-1]
    assert net_step(r) == -1

def test_pattern_set_nonempty():
    assert "AAAB" in PATTERNS and "AB" in PATTERNS

def test_aaab_same_note():
    motifs, refs, conns = build_combination(A, B, "AAAB", "invert", "same")
    assert refs == ["A","A","A","B"]
    assert conns == [None, -1, -1, 0]
    assert set(motifs) == {"A","B"}

def test_prime_pattern_uses_transform():
    motifs, refs, conns = build_combination(A, B, "AAA'B", "retrograde", "climb")
    assert refs == ["A","A","P","B"]
    assert "P" in motifs
    assert [u["step"] for u in motifs["P"]["units"]] == [0,1,-1,-1]
    assert conns == [None, 0, 0, 0]

def test_template_structure():
    motifs, refs, conns = build_combination(A, B, "AAB", "invert", "same")
    t = make_template(motifs, refs, conns, key="C", scale="Major", bpm=84.0, seed=7)
    names = {m["name"] for m in t["motifs"]}
    assert names == {"A","B"}
    assert all(m.get("userProvided") for m in t["motifs"])
    ph = t["parts"][0]["passages"]["Main"]["phrases"][0]
    assert [f["motifName"] for f in ph["figures"]] == ["A","A","B"]
    assert len(ph["connectors"]) == len(ph["figures"])
    assert ph["connectors"][0] is None
    assert t["masterSeed"] == 7
    total = sum(u["duration"] for r in refs for u in motifs[r]["units"])
    assert t["sections"][0]["beats"] == total


# --------------------------------------------------------------------------- #
# backlog #10 — per-occurrence variants across the transform library
# --------------------------------------------------------------------------- #
LONG = {"units": [{"duration": 0.5, "step": 0}, {"duration": 0.5, "step": 1},
                  {"duration": 0.25, "step": 2}, {"duration": 0.5, "step": -1},
                  {"duration": 0.25, "step": -1}, {"duration": 1.0, "step": 1}]}


def test_tokens_independent_vs_involution():
    # Involutions share ONE variant motif; independent ops get one each.
    assert _tokens_v2("AA'A''B", "invert") == ["A", "P", "P", "B"]
    for op in ("rotate", "ornament", "augment", "vary_tail", "mixed"):
        assert _tokens_v2("AA'A''B", op) == ["A", "V1", "V2", "B"], op


def test_make_variant_differs_per_occurrence():
    # The bug this fixes: A' and A'' used to be the same operation (vary_tail)
    # no matter which transform was chosen. Parameterized ops must now differ
    # between occurrence 1 and occurrence 2.
    rng = random.Random(7)
    for op in ("rotate", "augment", "diminish", "expand_intervals",
               "compress_intervals"):
        v1, l1 = make_variant(LONG, op, 1, None, rng, seen=[LONG])
        v2, l2 = make_variant(LONG, op, 2, None, rng, seen=[LONG, v1])
        assert l1.startswith(op) and l2.startswith(op), (l1, l2)
        assert v1 != v2, f"{op}: occurrence 1 and 2 produced identical figures"
        assert v1 != LONG and v2 != LONG, f"{op}: variant equals the original"
    # compress_intervals is the one that needs the fallback (it saturates at
    # min step magnitude 1), so the label must say so rather than lying.
    c1, _ = make_variant(LONG, "compress_intervals", 1, None, rng, seen=[LONG])
    c2, lbl = make_variant(LONG, "compress_intervals", 2, None, rng,
                           seen=[LONG, c1])
    assert "+" in lbl, f"expected a fallback label, got {lbl!r}"


def test_make_variant_preserves_anchor_invariant():
    rng = random.Random(11)
    for op in INDEPENDENT_OPS:
        if op == "vary_tail":
            continue                       # needs a trained model
        v, _ = make_variant(LONG, op, 1, None, rng)
        assert v["units"][0]["step"] == 0, op
        assert v is not LONG and v["units"] is not LONG["units"], op
    assert INVOLUTION_OPS == {"invert", "retrograde"}
    assert set(MIXED_MENU) <= set(INDEPENDENT_OPS)


def test_build_phrase_v3_reports_distinct_ops():
    rng = random.Random(3)
    _ml, refs, _conns, info = build_phrase_v3(
        LONG, LONG, "AA'A''B", "rotate", "step_up", None, rng)
    assert refs == ["A", "V1", "V2", "B"]
    assert info["ops"] == {"V1": "rotate", "V2": "rotate"}


def test_v3_patterns_exercise_multi_prime():
    assert any(p.count("'") >= 2 for p in V3_PATTERNS)


if __name__ == "__main__":
    test_net_step(); test_invert(); test_retrograde()
    test_pattern_set_nonempty(); test_aaab_same_note(); test_prime_pattern_uses_transform()
    test_template_structure()
    test_tokens_independent_vs_involution()
    test_make_variant_differs_per_occurrence()
    test_make_variant_preserves_anchor_invariant()
    test_build_phrase_v3_reports_distinct_ops()
    test_v3_patterns_exercise_multi_prime()
    print("OK")
