"""Plain-assert tests for markov_pool Tasks 1-2."""
from markov_pool import classify_contour, within_leap_cap, tag_atom, accumulate


def test_contour_cases():
    assert classify_contour([0,1,1,1])      == "Up"      # C-D-E-F
    assert classify_contour([0,-1,-1,-1])   == "Down"    # C-B-A-G
    assert classify_contour([0,1,1,-1,-1])  == "Arch"    # C-D-E-D-C
    assert classify_contour([0,-1,-1,1,1])  == "Valley"  # C-B-A-B-C
    assert classify_contour([0,1,-1,1])     == "Level"   # C-D-C-D hover
    assert classify_contour([0,0,0])        == "Level"   # repeated
    assert classify_contour([0,1,1,1,-1])   == "Up"      # C-D-E-F-E (R=2: pullback<2)
    assert classify_contour([0,4,-4])       == "Arch"    # C-G-C


def test_leap_cap():
    assert within_leap_cap([0,1,7,-7]) is True
    assert within_leap_cap([0,8]) is False


def test_tag_atom():
    a = tag_atom([0,1,1], [0.5,0.5,0.5])
    assert a["noteCount"] == 3
    assert a["totalBeats"] == 1.5
    assert a["contour"] == "Up"
    assert a["count"] == 1
    assert a["units"] == [{"duration":0.5,"step":0},{"duration":0.5,"step":1},{"duration":0.5,"step":1}]


def test_accumulate_weights():
    pool = {}
    accumulate(pool, [0,1], [0.5,0.5])
    accumulate(pool, [0,1], [0.5,0.5])     # same content
    accumulate(pool, [0,-1], [0.5,0.5])    # different
    counts = sorted(a["count"] for a in pool.values())
    assert counts == [1, 2]


if __name__ == "__main__":
    test_contour_cases()
    test_leap_cap()
    test_tag_atom()
    test_accumulate_weights()
    print("OK")
