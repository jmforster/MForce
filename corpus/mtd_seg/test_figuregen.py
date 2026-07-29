"""Plain-assert tests for figuregen (FigureGenerator interface + n-gram family).

Key equivalence: NGramModel(order=2, thresh=3) must reproduce the shipped
markov_model.MarkovModel's backoff counters exactly — the bake-off's "existing
Markov" row has to be the real baseline, not a lookalike.
"""
import random

from figuregen import (load_corpus, NGramModel, NGramGenerator,
                       UniformGenerator, build_methods)
from markov_model import MarkovModel


def _streams():
    streams, meta = load_corpus()
    return streams, meta


def test_backoff_matches_shipped_markov():
    streams, _ = _streams()
    ng = NGramModel(streams, order=2, thresh=3)
    mk = MarkovModel(streams)
    # unigram, order-1, order-2 counters must be identical dicts of Counters.
    assert dict(ng.tables[0][()]) == dict(mk.uni)
    assert len(ng.tables[1]) == len(mk.ctx1)
    assert len(ng.tables[2]) == len(mk.ctx2)
    # spot-check a populated order-2 context resolves to the same Counter
    ctx2 = next(iter(mk.ctx2))
    assert dict(ng.tables[2][ctx2]) == dict(mk.ctx2[ctx2])
    # same backed-off distribution for a real 2-token history
    hist = list(ctx2)
    assert ng.dist(hist) == mk.dist(hist)


def test_bos_never_emitted():
    streams, _ = _streams()
    ng = NGramModel(streams, order=2)
    bos_steps = {s[0][0] for s in streams}          # sentinel step(s)
    for tok in ng.alphabet:
        assert tok[0] not in bos_steps, "BOS sentinel leaked into emissions"


def test_figure_invariants():
    streams, meta = _streams()
    rng = random.Random(0)
    for gen in build_methods(streams, meta):
        for k in (3, 5, 8):
            steps, pulses = gen.figure(k, rng)
            assert len(steps) == k and len(pulses) == k, gen.name
            assert steps[0] == 0, f"{gen.name}: step[0] must anchor at 0"
            assert all(p > 0 for p in pulses), f"{gen.name}: non-positive pulse"


def test_transitions_length():
    streams, meta = _streams()
    rng = random.Random(1)
    for gen in build_methods(streams, meta):
        toks = gen.transitions(12, rng)
        assert len(toks) == 12, gen.name


def test_addk_reaches_full_alphabet():
    streams, _ = _streams()
    ng = NGramModel(streams, order=2, add_k=0.1)
    # With smoothing, every alphabet token has non-zero prob from any context.
    d = ng.dist([ng.alphabet[0]])
    assert len(d) == len(ng.alphabet)
    assert all(p > 0 for p in d.values())


def test_uniform_uses_alphabets_only():
    _, meta = _streams()
    rng = random.Random(2)
    gen = UniformGenerator(meta)
    steps, pulses = gen.figure(6, rng)
    assert all(s in meta["steps"] for s in steps[1:])
    assert all(p in meta["pulses"] for p in pulses[1:])


if __name__ == "__main__":
    test_backoff_matches_shipped_markov()
    test_bos_never_emitted()
    test_figure_invariants()
    test_transitions_length()
    test_addk_reaches_full_alphabet()
    test_uniform_uses_alphabets_only()
    print("OK")
