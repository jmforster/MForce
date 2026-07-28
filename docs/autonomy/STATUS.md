# Status — open this file first

Updated: 2026-07-28, end of comp run 3 (`go comp`; Wolfie)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 3 (2026-07-28), 3/3 fronts: Phase-2 combination phrases scored (long combos mean 0.705, top AAA'B 0.83–0.865); range-runaway guard landed (19%→0%, composite-neutral); outlier mode added — checked a suspected scorer rep-hole against the corpus and it held (no change) | corpus survey (#3) or FigureGenerator bake-off (#4) | **3 items** — parallel A/B [listen], scorer sanity [read], Phase-2 phrases [listen] |
| dsp | Dipsy | run 2 (2026-07-27), 2/3 fronts: calibrated motion v2 (6.4c vs 7.3c target); CMA-ES spec written | **carried over:** expansion sweep; then CMA-ES stage a | **3 items** — v5 grounded A/B incl. calibrated [listen], v4 residue ladder [listen], UI fix check [look] |

Reports: dsp/reports/2026-07-27-dipsy-run2.md ·
comp/reports/2026-07-28-wolfie-run3.md

Run-3 highlights (comp): banked Phase-2 combination-phrase renderer taken
from "committed but never scored" to scored + guarded + audition-queued.
Combination structure does real plausibility work (long combos reach
corpus-anchor territory; bare AB pairs are the weak tail). New range guard
kills the out-of-corpus-range failure mode for free. A suspected scorer
rep-hole (outliers scoring high) was checked against the corpus and did NOT
hold — corpus repetition genuinely reaches coverage 0.86–0.97, so the
composite is faithful on first-order stats; no scorer change made. The
remaining gap is a higher-order monotony screen (#9, optional hardening).

Next "go": comp = corpus survey (#3) / FigureGenerator bake-off (#4);
dsp = Dipsy expansion sweep + CMA-ES stage a. Review verdicts fold in
whenever you send them.

How this works: [WORKFLOW.md](WORKFLOW.md)
