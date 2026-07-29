"""FigureGenerator plugin interface + n-gram family (comp backlog #4).

Formalizes the "how do we invent an atomic figure" step behind ONE interface so
different methods can be swapped and compared on the corpus-statistics harness
(score_generated.py). Every generator emits the same thing the composer already
consumes: a (step, pulse) figure with step[0]==0 (anchored), plus a free-run
`transitions()` mode for stable distributional scoring.

Token = (dstep:int, pulse:float), identical to markov_tokenize / markov_model.
The order-2 stupid-backoff NGramModel here reproduces the existing
markov_model.MarkovModel semantics (THRESH=3) — verified in test_figuregen.

Methods registered (see METHODS at bottom):
  uniform         uniform over the observed step/pulse alphabets (dumb baseline)
  unigram         order-0 corpus marginal (frequency-weighted, no context)
  ngram1          order-1 (previous token only)
  ngram2_backoff  order-2 stupid-backoff  == the shipped Markov model
  ngram3_backoff  order-3 stupid-backoff
  ngram2_addk     order-2 backoff + additive (add-k) smoothing over the alphabet

Import-only (no side effects); bake_off.py drives it.
"""
import json
import pathlib
import random
from collections import Counter, defaultdict

ROOT = pathlib.Path(__file__).resolve().parent
TOKENS = ROOT / "markov_tokens.json"
THRESH = 3   # min observations to trust a context before backing off (matches markov_model)


def _tok(pair):
    return (int(pair[0]), float(pair[1]))


def load_corpus(path=TOKENS):
    """Return (streams, meta). streams: list of token-lists (token0 = BOS sentinel)."""
    data = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
    streams = [[_tok(p) for p in s] for s in data["streams"]]
    meta = {
        "steps": [int(x) for x in data["alphabet"]["steps"]],
        "pulses": [float(x) for x in data["alphabet"]["pulses"]],
        "pulse0": [float(x) for x in data["pulse0_all"]],
    }
    return streams, meta


# --------------------------------------------------------------------------- #
# Generic n-gram model (order-0..N stupid-backoff, optional add-k smoothing)
# --------------------------------------------------------------------------- #
class NGramModel:
    """Order-N n-gram over (dstep,pulse) tokens with stupid-backoff.

    tables[n] maps an n-token context -> Counter(next). n=0 is the unigram
    marginal (context ()). BOS (streams[*][0]) is used only as CONTEXT — it is
    never counted as an emission (loop skips i==0), so it can never be sampled
    mid-figure, exactly as in markov_model.

    Backoff: from the longest available context down to unigram, take the first
    context with >= thresh observations. add_k>0 additively smooths the chosen
    context over the full observed alphabet (Laplace), which keeps rare-but-legal
    transitions reachable instead of hard-zero.
    """

    def __init__(self, streams, order=2, thresh=THRESH, add_k=0.0):
        self.order = order
        self.thresh = thresh
        self.add_k = add_k
        self.tables = [defaultdict(Counter) for _ in range(order + 1)]
        for s in streams:
            for i, t in enumerate(s):
                if i == 0:
                    continue            # BOS: context only, never an emission
                for n in range(0, order + 1):
                    if i - n < 0:
                        break           # would need tokens before BOS
                    ctx = tuple(s[i - n:i])
                    self.tables[n][ctx][t] += 1
        self.uni = self.tables[0][()]
        self.alphabet = list(self.uni.keys())
        self.bos_tokens = [s[0] for s in streams]

    def _counter_for(self, history):
        """Backed-off Counter for the given history (list of tokens)."""
        for n in range(min(self.order, len(history)), 0, -1):
            c = self.tables[n].get(tuple(history[-n:]))
            if c and sum(c.values()) >= self.thresh:
                return c
        return self.uni

    def dist(self, history):
        """Normalized {token: prob} from the backed-off (optionally smoothed) counter."""
        c = self._counter_for(history)
        if self.add_k > 0.0:
            total = sum(c.values()) + self.add_k * len(self.alphabet)
            return {t: (c.get(t, 0) + self.add_k) / total for t in self.alphabet}
        total = sum(c.values())
        return {t: n / total for t, n in c.items()}

    def next_token(self, history, rng):
        if self.add_k > 0.0:
            d = self.dist(history)
            toks = list(d.keys())
            return rng.choices(toks, weights=[d[t] for t in toks], k=1)[0]
        c = self._counter_for(history)
        toks = list(c.keys())
        return rng.choices(toks, weights=list(c.values()), k=1)[0]


# --------------------------------------------------------------------------- #
# FigureGenerator plugin interface
# --------------------------------------------------------------------------- #
class FigureGenerator:
    """A method for inventing atomic figures + free-run transition streams.

    figure(k, rng)      -> (steps, pulses): k notes, steps[0]==0 (anchored atom,
                           what PoolFigureBuilder/markov_phrase consume).
    transitions(n, rng) -> [(dstep, pulse), ...]: n free-run transitions from a
                           real opening, for distributional scoring (bake-off).
    """

    name = "base"

    def figure(self, k, rng):
        raise NotImplementedError

    def transitions(self, n, rng):
        raise NotImplementedError


class NGramGenerator(FigureGenerator):
    def __init__(self, name, model):
        self.name = name
        self.model = model

    def figure(self, k, rng):
        bos = rng.choice(self.model.bos_tokens)     # (BOS_STEP, note-0 duration)
        history = [bos]
        transitions = []
        for _ in range(k - 1):
            t = self.model.next_token(history, rng)
            transitions.append(t)
            history.append(t)
        steps = [0] + [t[0] for t in transitions]
        pulses = [bos[1]] + [t[1] for t in transitions]
        return steps, pulses

    def transitions(self, n, rng):
        bos = rng.choice(self.model.bos_tokens)
        history = [bos]
        out = []
        for _ in range(n):
            t = self.model.next_token(history, rng)
            out.append(t)
            history.append(t)
        return out


class UniformGenerator(FigureGenerator):
    """Dumb baseline: dstep uniform over the observed step alphabet, pulse uniform
    over the observed pulse alphabet. No corpus structure whatsoever — the floor
    the corpus-derived methods must clear."""

    name = "uniform"

    def __init__(self, meta):
        self.steps = meta["steps"]
        self.pulses = meta["pulses"]
        self.pulse0 = meta["pulse0"]

    def _tok(self, rng):
        return (rng.choice(self.steps), rng.choice(self.pulses))

    def figure(self, k, rng):
        toks = [self._tok(rng) for _ in range(k - 1)]
        steps = [0] + [t[0] for t in toks]
        pulses = [rng.choice(self.pulse0)] + [t[1] for t in toks]
        return steps, pulses

    def transitions(self, n, rng):
        return [self._tok(rng) for _ in range(n)]


# --------------------------------------------------------------------------- #
# Registry
# --------------------------------------------------------------------------- #
def build_methods(streams, meta):
    """Instantiate the bake-off roster. Order = report order."""
    return [
        UniformGenerator(meta),
        NGramGenerator("unigram",        NGramModel(streams, order=0)),
        NGramGenerator("ngram1",         NGramModel(streams, order=1)),
        NGramGenerator("ngram2_backoff", NGramModel(streams, order=2)),
        NGramGenerator("ngram3_backoff", NGramModel(streams, order=3)),
        NGramGenerator("ngram2_addk",    NGramModel(streams, order=2, add_k=0.1)),
    ]
