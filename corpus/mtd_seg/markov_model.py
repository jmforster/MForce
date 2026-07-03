"""Joint (step-delta, pulse) Markov model, order-2 with stupid-backoff.

Token = (dstep:int, pulse:float). Built from markov_tokens.json streams.
Backoff: try order-2 context; if it has fewer than THRESH observations, fall to
order-1; if that's also thin, fall to the unigram marginal.

This module is import-only (no side effects); markov_generate.py drives it.
"""
import json, random, pathlib
from collections import Counter, defaultdict

ROOT       = pathlib.Path(__file__).resolve().parent
TOKENS     = ROOT / "markov_tokens.json"
THRESH     = 3   # min observations to trust a context before backing off


def _tok(pair):
    return (int(pair[0]), float(pair[1]))


class MarkovModel:
    def __init__(self, streams):
        self.ctx2 = defaultdict(Counter)   # (t-2,t-1) -> Counter(next)
        self.ctx1 = defaultdict(Counter)   # (t-1,)    -> Counter(next)
        self.uni  = Counter()              # ()        -> Counter(next)
        for s in streams:
            toks = [_tok(p) for p in s]
            # toks[0] is the BOS start token: used only as context, never an emission
            # (so it can never be sampled mid-figure).
            for i, t in enumerate(toks):
                if i >= 1:
                    self.uni[t] += 1
                    self.ctx1[(toks[i-1],)][t] += 1
                if i >= 2:
                    self.ctx2[(toks[i-2], toks[i-1])][t] += 1
        self.alphabet = list(self.uni.keys())

    @classmethod
    def load(cls, path=TOKENS):
        data = json.loads(pathlib.Path(path).read_text(encoding="utf-8"))
        m = cls(data["streams"])
        m.bos_tokens = [_tok(s[0]) for s in data["streams"]]  # start tokens (carry note-0 dur)
        m.starts = [_tok(p) for p in data["starts"]]          # note-1 tokens (diagnostic seed)
        m.pulse0 = list(data["pulse0_all"])
        m.step_alphabet  = [int(x) for x in data["alphabet"]["steps"]]
        m.pulse_alphabet = [float(x) for x in data["alphabet"]["pulses"]]
        return m

    def _counter_for(self, history):
        """Return the Counter chosen by backoff for the given history (list of tokens)."""
        if len(history) >= 2:
            c = self.ctx2.get((history[-2], history[-1]))
            if c and sum(c.values()) >= THRESH:
                return c, 2
        if len(history) >= 1:
            c = self.ctx1.get((history[-1],))
            if c and sum(c.values()) >= THRESH:
                return c, 1
        return self.uni, 0

    def dist(self, history):
        """Normalized {token: prob} from the backed-off counter."""
        c, _ = self._counter_for(history)
        total = sum(c.values())
        return {t: n / total for t, n in c.items()}

    def next_token(self, history, rng=random):
        c, _ = self._counter_for(history)
        toks = list(c.keys())
        weights = list(c.values())
        return rng.choices(toks, weights=weights, k=1)[0]

    def top_paths(self, k, n=8, beam=60):
        """n most-probable (k-note => k-1 transition) figures via beam search.
        Returns list of (logprob, [token,...])."""
        import math
        # Seed beam from the most probable opening tokens (real-start marginal).
        start = Counter(getattr(self, "starts", self.alphabet))
        st_total = sum(start.values()) or 1
        beams = [(math.log(start[t] / st_total), [t]) for t in start]
        beams.sort(reverse=True)
        beams = beams[:beam]
        for _ in range(k - 2):                 # already have 1 transition token
            nxt = []
            for lp, seq in beams:
                for t, p in self.dist(seq).items():
                    nxt.append((lp + math.log(p), seq + [t]))
            nxt.sort(reverse=True)
            beams = nxt[:beam]
        beams.sort(reverse=True)
        return beams[:n]
