"""Integration tests (plain asserts) — render via mforce_cli, verify pitch contour."""
import glob, pathlib, subprocess
from markov_phrase import (build_combination, make_template,
                           predict_relative_semitones, render_template)

REPO = pathlib.Path(__file__).resolve().parent.parent.parent

A = {"units": [{"duration":0.5,"step":0},{"duration":0.5,"step":1},
               {"duration":0.5,"step":1},{"duration":0.5,"step":-1}]}
B = {"units": [{"duration":1.0,"step":0},{"duration":1.0,"step":2}]}


def test_engine_matches_prediction():
    motifs, refs, conns = build_combination(A, B, "AAB", "invert", "same")
    t = make_template(motifs, refs, conns, seed=1)
    predicted = predict_relative_semitones(motifs, refs, conns)
    notes = render_template(t, "renders/_phrase_test")
    realized = [n - notes[0] for n in notes]
    assert realized == predicted, f"realized {realized} != predicted {predicted}"


def test_batch_smoke():
    subprocess.run(["python", "markov_phrase.py", "--n", "2", "--seed", "5"], check=True)
    wavs = glob.glob(str(REPO / "renders/markov_phrases/*.wav"))
    assert len(wavs) >= 2


if __name__ == "__main__":
    test_engine_matches_prediction()
    test_batch_smoke()
    print("OK")
