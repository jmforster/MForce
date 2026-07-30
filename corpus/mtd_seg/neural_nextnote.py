"""Neural next-note model (comp backlog #3, Matt G1 "LLM-like prediction of the
next note vs. Markov"). A numpy-only feed-forward neural language model over the
same (dstep, pulse) token stream the Markov/n-gram methods use, so it slots into
the FigureGenerator bake-off as a head-to-head entrant.

Architecture (Bengio 2003 neural LM, the smallest thing that is genuinely
"neural" rather than count-based):

    context of C previous tokens
        -> learned token embeddings (V x emb), looked up + concatenated
        -> one tanh hidden layer (C*emb -> hid)
        -> softmax over the V-token vocabulary

Trained by minibatch SGD on next-token cross-entropy over the corpus streams,
with hand-written backprop (no autograd, no torch). Distinct from the n-grams in
three ways that matter: (1) it learns a distributed representation of tokens
(similar intervals/rhythms share embedding structure), (2) it generalizes to
unseen contexts by interpolation in embedding space instead of hard backoff,
(3) capacity is fixed (hid) rather than growing with the context table.

BOS: every stream starts with a (-999, note0-dur) sentinel. All sentinels
collapse to a single BOS id used only as left-padding context; it is masked out
of the output distribution so it can never be emitted mid-figure — exactly the
Markov contract.

Standalone:  python neural_nextnote.py [--tokens x.json] [--epochs N] ...
    trains, prints the loss curve + held-out perplexity, samples a few figures.
As a bake-off entrant: figuregen.build_methods soft-imports make_neural_generator
so `python bake_off.py [--tokens ...]` includes it automatically.
"""
import argparse
import pathlib
import random
import sys

import numpy as np

HERE = pathlib.Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from figuregen import FigureGenerator, load_corpus   # noqa: E402

BOS_STEP = -999


# --------------------------------------------------------------------------- #
# Vocabulary
# --------------------------------------------------------------------------- #
def build_vocab(streams):
    """id 0 = BOS (context-only, never emitted). ids 1..V-1 = observed emission
    tokens (dstep, pulse), sorted for determinism."""
    toks = set()
    for s in streams:
        for i, t in enumerate(s):
            if i == 0:
                continue
            toks.add((int(t[0]), float(t[1])))
    real = sorted(toks)
    id2tok = [None] + real
    tok2id = {t: i + 1 for i, t in enumerate(real)}
    return id2tok, tok2id


def _tokid(t, tok2id):
    step = int(t[0])
    return 0 if step == BOS_STEP else tok2id[(step, float(t[1]))]


# --------------------------------------------------------------------------- #
# The neural language model
# --------------------------------------------------------------------------- #
class NeuralNextNote:
    def __init__(self, streams, context=3, emb=16, hid=64, epochs=10,
                 lr=0.2, batch=256, seed=1234, val_frac=0.1, verbose=False):
        self.C = context
        self.id2tok, self.tok2id = build_vocab(streams)
        self.V = len(self.id2tok)
        self.bos_tokens = [tuple(s[0]) for s in streams]     # carry note-0 dur
        rs = np.random.RandomState(seed)

        # examples: (context ids, target id), left-padded with BOS at seq start
        X, Y = [], []
        for s in streams:
            ids = [_tokid(t, self.tok2id) for t in s]
            for i in range(1, len(ids)):
                ctx = ids[max(0, i - self.C):i]
                ctx = [0] * (self.C - len(ctx)) + ctx        # left-pad BOS
                X.append(ctx)
                Y.append(ids[i])
        X = np.asarray(X, dtype=np.int64)
        Y = np.asarray(Y, dtype=np.int64)

        # train/val split (held-out perplexity = honest generalization number)
        perm = rs.permutation(len(X))
        X, Y = X[perm], Y[perm]
        nval = int(len(X) * val_frac)
        self.Xtr, self.Ytr = X[nval:], Y[nval:]
        self.Xval, self.Yval = X[:nval], Y[:nval]

        # params
        k = emb
        self.E = rs.randn(self.V, k).astype(np.float64) * 0.1
        self.W1 = rs.randn(self.C * k, hid).astype(np.float64) * (1.0 / np.sqrt(self.C * k))
        self.b1 = np.zeros(hid)
        self.W2 = rs.randn(hid, self.V).astype(np.float64) * (1.0 / np.sqrt(hid))
        self.b2 = np.zeros(self.V)
        self.emb, self.hid = k, hid

        self.history = self._train(epochs, lr, batch, rs, verbose)

    # ---- forward / backward -------------------------------------------------
    def _forward(self, Xb):
        e = self.E[Xb]                                   # (B,C,emb)
        x = e.reshape(len(Xb), self.C * self.emb)        # (B,C*emb)
        hpre = x @ self.W1 + self.b1
        h = np.tanh(hpre)                                # (B,hid)
        logits = h @ self.W2 + self.b2                   # (B,V)
        logits -= logits.max(axis=1, keepdims=True)
        p = np.exp(logits)
        p /= p.sum(axis=1, keepdims=True)
        return x, h, p

    def _loss(self, X, Y):
        _, _, p = self._forward(X)
        return float(-np.log(p[np.arange(len(Y)), Y] + 1e-12).mean())

    def _train(self, epochs, lr, batch, rs, verbose):
        hist = []
        n = len(self.Xtr)
        for ep in range(epochs):
            order = rs.permutation(n)
            tot = 0.0
            for b0 in range(0, n, batch):
                idx = order[b0:b0 + batch]
                Xb, Yb = self.Xtr[idx], self.Ytr[idx]
                B = len(Xb)
                x, h, p = self._forward(Xb)
                tot += float(-np.log(p[np.arange(B), Yb] + 1e-12).sum())
                # backprop
                dlog = p
                dlog[np.arange(B), Yb] -= 1.0
                dlog /= B
                dW2 = h.T @ dlog
                db2 = dlog.sum(axis=0)
                dh = dlog @ self.W2.T
                dhpre = dh * (1.0 - h * h)
                dW1 = x.T @ dhpre
                db1 = dhpre.sum(axis=0)
                dx = (dhpre @ self.W1.T).reshape(B, self.C, self.emb)
                dE = np.zeros_like(self.E)
                np.add.at(dE, Xb, dx)
                # SGD
                self.W2 -= lr * dW2
                self.b2 -= lr * db2
                self.W1 -= lr * dW1
                self.b1 -= lr * db1
                self.E -= lr * dE
            tr = tot / n
            va = self._loss(self.Xval, self.Yval) if len(self.Xval) else float("nan")
            hist.append((ep, tr, va))
            if verbose:
                print(f"  epoch {ep:2d}  train_ce={tr:.4f}  "
                      f"val_ce={va:.4f}  val_ppl={np.exp(va):.2f}")
        return hist

    def val_perplexity(self):
        if not len(self.Xval):
            return float("nan")
        return float(np.exp(self._loss(self.Xval, self.Yval)))

    # ---- inference ----------------------------------------------------------
    def dist(self, history):
        """{token: prob} over emission tokens for the given token history."""
        ids = [_tokid(t, self.tok2id) for t in history]
        ctx = ids[-self.C:]
        ctx = [0] * (self.C - len(ctx)) + ctx
        _, _, p = self._forward(np.asarray([ctx], dtype=np.int64))
        p = p[0].copy()
        p[0] = 0.0                                       # never emit BOS
        s = p.sum()
        if s <= 0:
            p[1:] = 1.0 / (self.V - 1)
        else:
            p /= s
        return {self.id2tok[i]: p[i] for i in range(1, self.V)}

    def next_token(self, history, rng):
        d = self.dist(history)
        toks = list(d.keys())
        return rng.choices(toks, weights=[d[t] for t in toks], k=1)[0]


# --------------------------------------------------------------------------- #
# FigureGenerator wrapper (mirrors figuregen.NGramGenerator exactly)
# --------------------------------------------------------------------------- #
class NeuralGenerator(FigureGenerator):
    def __init__(self, name, model):
        self.name = name
        self.model = model

    def figure(self, k, rng):
        bos = rng.choice(self.model.bos_tokens)
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


def make_neural_generator(streams, meta, name="neural_mlp", **kw):
    """Factory used by figuregen.build_methods (soft-imported) and standalone."""
    return NeuralGenerator(name, NeuralNextNote(streams, **kw))


# --------------------------------------------------------------------------- #
# Standalone: train + report + sample
# --------------------------------------------------------------------------- #
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--tokens", default=None, help="token json (default MTD)")
    ap.add_argument("--context", type=int, default=3)
    ap.add_argument("--emb", type=int, default=16)
    ap.add_argument("--hid", type=int, default=64)
    ap.add_argument("--epochs", type=int, default=10)
    ap.add_argument("--lr", type=float, default=0.2)
    ap.add_argument("--seed", type=int, default=1234)
    args = ap.parse_args()

    streams, meta = load_corpus(args.tokens) if args.tokens else load_corpus()
    tag = pathlib.Path(args.tokens).stem if args.tokens else "MTD"
    print(f"[neural] corpus={tag} streams={len(streams)} "
          f"context={args.context} emb={args.emb} hid={args.hid}")
    m = NeuralNextNote(streams, context=args.context, emb=args.emb,
                       hid=args.hid, epochs=args.epochs, lr=args.lr,
                       seed=args.seed, verbose=True)
    print(f"[neural] vocab={m.V} train={len(m.Xtr)} val={len(m.Xval)} "
          f"final val_ppl={m.val_perplexity():.2f}")

    rng = random.Random(args.seed)
    gen = NeuralGenerator("neural_mlp", m)
    print("\nsample figures (steps / pulses):")
    for k in (4, 5, 6, 6, 7):
        steps, pulses = gen.figure(k, rng)
        ss = " ".join(f"{s:+d}" if i else "0" for i, s in enumerate(steps))
        ps = " ".join(f"{p:g}" for p in pulses)
        print(f"  k={k}: [{ss}]  [{ps}]")


if __name__ == "__main__":
    main()
