# Status — open this file first

Updated: 2026-07-29 — dsp run 5 (Dipsy, autonomous) · comp run 3 (Wolfie)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 3 (2026-07-28), 3/3 fronts: Phase-2 combination phrases scored (long combos mean 0.705, top AAA'B 0.83–0.865); range-runaway guard landed (19%→0%, composite-neutral); outlier mode added — checked a suspected scorer rep-hole against the corpus and it held (no change); corpus survey done (#3) | ingest Nottingham-Jukedeck (#3a) then FigureGenerator bake-off (#4) | **4 items** — parallel A/B [listen], scorer sanity [read], Phase-2 phrases [listen], Essen license [read] |
| dsp | Dipsy | run 5 (2026-07-29), 3/3 fronts, all landed+committed: (1) UI paramMap-curve preservation fixed — load→save round-trip + keyboard playback now honor curves (item 3b), verified via new headless `--roundtrip` mode on all 4 v6 patches; v6+ safe to re-save from UI. (2) Novelty metric shipped (item 4) — research/novelty/, 36-dim MFCC+spectral embedding, distance-from-library scoring, selftest PASS + real-render ordering validated, `manifest` subcommand wires it into explore. (3) Recursive expansion sweep (item 5) — 18 regimes incl. 6 rule-breakers, novelty-ranked, survivors queued for listen | item 6 (FormantSequence deep-dive, now novelty-gatable) → item 7 (oversampled FM) / item 8 (additive perf) | **5 items** — expand-sweep survivors [listen], CMA-ES stage-d A/B [listen], v6 curve ladder [listen], + prior |

Reports: dsp/reports/2026-07-29-dipsy-run5.md ·
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

Run-5 highlights (dsp): cleared the three top non-gated items. The v6 curve
patches are no longer a UI trap — load/save/playback all preserve curves now
(headless `--roundtrip` mode proves it). The novelty metric is live and
self-verifying, which unblocked the first recursive-expansion sweep: 18
ExpandRule regimes ranked by timbral distance, with two rule-breakers
(golden-ratio and π semitone spacing) landing in the top third — those are the
"accidental discovery" bets for your ears. recurse 2-4 stays parked behind the
additive-performance work (item 8).

Next "go": comp = ingest Nottingham-Jukedeck (#3a) → FigureGenerator
bake-off (#4); dsp = item 6 (FormantSequence deep-dive, now novelty-gatable),
then item 7 (oversampled FM) or item 8 (additive perf — also the gate for
recurse 2-4 expansion). Three dsp listen items now waiting (expand-sweep
survivors, CMA-ES stage-d, v6 curve ladder). Verdicts fold in whenever you
send them.

How this works: [WORKFLOW.md](WORKFLOW.md)
