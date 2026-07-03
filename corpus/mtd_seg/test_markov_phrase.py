"""Unit tests (plain asserts, no pytest) for markov_phrase Tasks 1-3."""
from markov_phrase import (net_step, invert, retrograde,
                           build_combination, PATTERNS, make_template)

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


if __name__ == "__main__":
    test_net_step(); test_invert(); test_retrograde()
    test_pattern_set_nonempty(); test_aaab_same_note(); test_prime_pattern_uses_transform()
    test_template_structure()
    print("OK")
