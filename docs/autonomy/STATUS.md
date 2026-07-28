# Status — open this file first

Updated: 2026-07-28 — dsp run 4 (Dipsy, interactive) · comp run 3 (Wolfie)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 3 (2026-07-28), 3/3 fronts: Phase-2 combination phrases scored (long combos mean 0.705, top AAA'B 0.83–0.865); range-runaway guard landed (19%→0%, composite-neutral); outlier mode added — checked a suspected scorer rep-hole against the corpus and it held (no change); corpus survey done (#3) | ingest Nottingham-Jukedeck (#3a) then FigureGenerator bake-off (#4) | **4 items** — parallel A/B [listen], scorer sanity [read], Phase-2 phrases [listen], Essen license [read] |
| dsp | Dipsy | run 3 (2026-07-28), 3/3 fronts: CMA-ES optimizer stages a→c landed — patch scorer vs Iowa reference (stage-a check passes), pure-numpy CMA-ES core (Rosenbrock + resume verified), optimizer loop (100-eval smoke **1.28→0.945**, every term improved). Stage-d first result queued for review (smoke best re-rendered on full ladder, renders/cmaes_smoke/); full 600-eval run gated on render-speed + Matt's A/B read | full stage-d run (after speed fix); then novelty metric (#4) or expansion sweep (#5) | **4 items** — CMA-ES opt vs hand-tune A/B [listen], v5 grounded A/B [listen], v4 residue ladder [listen], UI fix check [look] |

Reports: dsp/reports/2026-07-28-dipsy-run3.md ·
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

Corpus survey landed (corpus_survey.md): ingest Nottingham-Jukedeck first
(GPLv3, MIDI-ready), Essen second pending a license call.

Run-3 highlights (dsp): the ears optimizer is real — a render→score harness
against the Iowa viola reference, a numpy CMA-ES core, and a warm-started
27-dim loop that already pulls a hand-tuned patch measurably closer to the
sample (1.28→0.945, biggest gain on the bow-noise/broadband term). Only
remaining blocker to a listenable result is eval speed: render is ~real-time
so 600 evals ≈ 2 h (stage d running now, resumable).

Next "go": comp = ingest Nottingham-Jukedeck (#3a) → FigureGenerator
bake-off (#4); dsp = pending Matt's CMA-ES A/B read — either launch the full
stage-d run (with a render-speed approach chosen) or adjust the scorer/vibrato
pin, then novelty metric (#4). Review verdicts fold in whenever you send them.

How this works: [WORKFLOW.md](WORKFLOW.md)
