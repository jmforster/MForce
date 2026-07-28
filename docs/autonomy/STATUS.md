# Status — open this file first

Updated: 2026-07-27, end of run 2 (both lanes; Wolfie's first shift)

| Lane | Dev | Run 2 | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | 3/3 fronts: scoring harness built+validated; parallel-period fix landed (`"parallel": true`); banked Phase-2 plan rediscovered & promoted | execute Phase-2 plan (combination phrases) | **2 items** — parallel A/B [listen], scorer sanity [read] |
| dsp | Dipsy | 2/3 fronts: calibrated motion v2 (6.4c vs 7.3c target); CMA-ES spec written | **carried over:** expansion sweep; then CMA-ES stage a | **3 items** — v5 grounded A/B incl. calibrated [listen], v4 residue ladder [listen], UI fix check [look] |

Reports: dsp/reports/2026-07-27-dipsy-run2.md ·
comp/reports/2026-07-27-wolfie-run2.md

Run-2 highlights: comp lane now has metric "ears" (score_generated.py —
corpus anchors 0.86-0.90, generated pieces below, ordering sane); period
parallelism is structural, not accidental; motion calibration converged to
within 13% of Iowa ground truth in 2 iterations.

Next "go": Dipsy expansion sweep + CMA-ES stage a; Wolfie Phase-2 plan
execution. Review verdicts fold in whenever you send them.

How this works: [WORKFLOW.md](WORKFLOW.md)
