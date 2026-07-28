"""Pure-numpy (mu/mu_w, lambda)-CMA-ES for the ml_ears patch optimizer.

Standard Hansen formulation (2016 tutorial), no external deps beyond numpy.
The optimizer works in an unbounded normalised space; bound/sigmoid mapping to
patch parameters lives in the encoder (stage c). State is fully picklable via
get_state/set_state for per-generation checkpointing / resume.

Self-test: `python cmaes.py` minimises 2-D Rosenbrock and asserts convergence.
"""
import numpy as np


class CMAES:
    def __init__(self, x0, sigma0, popsize=None, seed=0, bounds=None):
        self.dim = N = len(x0)
        self.mean = np.array(x0, dtype=float)
        self.sigma = float(sigma0)
        self.lam = int(popsize) if popsize else 4 + int(3 * np.log(N))
        self.mu = self.lam // 2
        # recombination weights (positive, log-decreasing, normalised)
        w = np.log(self.mu + 0.5) - np.log(np.arange(1, self.mu + 1))
        self.weights = w / w.sum()
        self.mueff = 1.0 / np.sum(self.weights ** 2)
        # adaptation constants
        self.cs = (self.mueff + 2) / (N + self.mueff + 5)
        self.ds = 1 + 2 * max(0, np.sqrt((self.mueff - 1) / (N + 1)) - 1) + self.cs
        self.cc = (4 + self.mueff / N) / (N + 4 + 2 * self.mueff / N)
        self.c1 = 2 / ((N + 1.3) ** 2 + self.mueff)
        self.cmu = min(1 - self.c1,
                       2 * (self.mueff - 2 + 1 / self.mueff) / ((N + 2) ** 2 + self.mueff))
        self.chiN = np.sqrt(N) * (1 - 1 / (4 * N) + 1 / (21 * N ** 2))
        # dynamic state
        self.pc = np.zeros(N)
        self.ps = np.zeros(N)
        self.C = np.eye(N)
        self.B = np.eye(N)
        self.D = np.ones(N)
        self.gen = 0
        self.rng = np.random.default_rng(seed)
        self.bounds = bounds     # (lo, hi) arrays or None; ask() clips samples
        self._z = None
        self._y = None

    def ask(self):
        """Return a (lam, dim) array of candidate solutions."""
        self._z = self.rng.standard_normal((self.lam, self.dim))
        self._y = (self.B * self.D) @ self._z.T          # (dim, lam)
        x = self.mean[:, None] + self.sigma * self._y     # (dim, lam)
        x = x.T
        if self.bounds is not None:
            x = np.clip(x, self.bounds[0], self.bounds[1])
        return x

    def tell(self, solutions, fitnesses):
        """Update the distribution from evaluated solutions (lower = better)."""
        solutions = np.asarray(solutions, float)
        order = np.argsort(fitnesses)
        N = self.dim
        # recover y for the sampled points (works whether or not clipped)
        y = (solutions - self.mean) / self.sigma           # (lam, dim)
        y_sorted = y[order]
        yw = self.weights @ y_sorted[:self.mu]             # (dim,)
        self.mean = self.mean + self.sigma * yw

        # C^{-1/2} = B diag(1/D) B^T
        C_invsqrt = (self.B * (1.0 / self.D)) @ self.B.T
        self.ps = (1 - self.cs) * self.ps + \
            np.sqrt(self.cs * (2 - self.cs) * self.mueff) * (C_invsqrt @ yw)
        self.gen += 1
        ps_norm = np.linalg.norm(self.ps)
        hs = ps_norm / np.sqrt(1 - (1 - self.cs) ** (2 * self.gen)) / self.chiN \
            < 1.4 + 2 / (N + 1)
        hs = 1.0 if hs else 0.0
        self.pc = (1 - self.cc) * self.pc + \
            hs * np.sqrt(self.cc * (2 - self.cc) * self.mueff) * yw

        # rank-mu update term
        rank_mu = (y_sorted[:self.mu] * self.weights[:, None]).T @ y_sorted[:self.mu]
        delta_hs = (1 - hs) * self.cc * (2 - self.cc)
        self.C = (1 - self.c1 - self.cmu) * self.C \
            + self.c1 * (np.outer(self.pc, self.pc) + delta_hs * self.C) \
            + self.cmu * rank_mu
        self.C = np.triu(self.C) + np.triu(self.C, 1).T    # enforce symmetry

        self.sigma *= np.exp((self.cs / self.ds) * (ps_norm / self.chiN - 1))

        # eigendecomposition (small N -> every generation is cheap)
        vals, self.B = np.linalg.eigh(self.C)
        vals = np.clip(vals, 1e-20, None)
        self.D = np.sqrt(vals)

    # ---- checkpoint / resume ----
    def get_state(self):
        return {k: (v.tolist() if isinstance(v, np.ndarray) else v)
                for k, v in {
                    "mean": self.mean, "sigma": self.sigma, "pc": self.pc,
                    "ps": self.ps, "C": self.C, "B": self.B, "D": self.D,
                    "gen": self.gen, "rng": self.rng.bit_generator.state}.items()}

    def set_state(self, s):
        self.mean = np.array(s["mean"]); self.sigma = s["sigma"]
        self.pc = np.array(s["pc"]); self.ps = np.array(s["ps"])
        self.C = np.array(s["C"]); self.B = np.array(s["B"]); self.D = np.array(s["D"])
        self.gen = s["gen"]; self.rng.bit_generator.state = s["rng"]


def fmin(objective, x0, sigma0, maxgen=1000, ftol=1e-10, seed=0, verbose=False):
    """Minimise objective(x)->float. Returns (best_x, best_f, es)."""
    es = CMAES(x0, sigma0, seed=seed)
    best_x, best_f = None, np.inf
    for g in range(maxgen):
        X = es.ask()
        F = np.array([objective(x) for x in X])
        es.tell(X, F)
        i = int(np.argmin(F))
        if F[i] < best_f:
            best_f, best_x = float(F[i]), X[i].copy()
        if verbose and g % 20 == 0:
            print(f"gen {g:4d}  best={best_f:.3e}  sigma={es.sigma:.3e}")
        if best_f < ftol or es.sigma < 1e-11:
            break
    return best_x, best_f, es


def _rosenbrock(x):
    return sum(100 * (x[i + 1] - x[i] ** 2) ** 2 + (1 - x[i]) ** 2
               for i in range(len(x) - 1))


if __name__ == "__main__":
    # 2-D Rosenbrock toy test (global min f=0 at [1,1]).
    bx, bf, es = fmin(_rosenbrock, [-1.2, 1.0], 0.3, maxgen=500, seed=1, verbose=True)
    print(f"\nbest x = {bx}  f = {bf:.3e}  gens = {es.gen}")
    assert bf < 1e-8, f"did not converge: f={bf}"
    assert np.allclose(bx, [1, 1], atol=1e-3), f"wrong optimum: {bx}"
    # 6-D as a mild dimensionality check
    bx6, bf6, es6 = fmin(_rosenbrock, np.zeros(6), 0.5, maxgen=2000, seed=2)
    print(f"6-D Rosenbrock: f = {bf6:.3e}  gens = {es6.gen}")
    assert bf6 < 1e-6, f"6-D did not converge: f={bf6}"
    print("\nCMA-ES self-test PASS")
