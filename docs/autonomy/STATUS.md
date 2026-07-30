# Status — open this file first

Updated: 2026-07-29 — dsp run 6 (Dipsy, interactive) · comp run 5 (Wolfie, interactive)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 5 (2026-07-29, interactive): Matt's 5 verdicts folded; monotone-B root-caused + fixed (selection bias, 0.38→0.19) and phrases regenerated; Nottingham ingested (1024 tunes) + folk bake-off (ranking corpus-stable); #9 v1 monotony screens landed (offenders 0.63-0.78, anchors 0.93) | contrast-aware fig B (#2), then neural next-note (#3) | **1 item** — regenerated phrases re-listen |
| dsp | Dipsy | run 6 (2026-07-29, interactive): FormantSequence sweep landed (item 6) — 15 regimes x drivers incl. audio-rate blend rule-breakers, novelty-ranked; detour caught 3 inert renders (formant bands narrower than harmonic spacing — sweep-design lesson logged) | item 7 (oversampled FM) / item 8 (additive perf) — carried | **4 listen items** — formantseq survivors (NEW), expand-sweep survivors, CMA-ES stage-d A/B, v6 curve ladder |

Reports: dsp/reports/2026-07-29-dipsy-run5.md ·
comp/reports/2026-07-29-wolfie-run4.md

Run-4 highlights (comp): formalized the FigureGenerator plugin interface and
stood up the method bake-off (#4) — the head-to-head that G1's "other methods
besides Markov" needs. Generic NGramModel is proven byte-for-byte equal to the
shipped Markov at order-2, so the baseline row is honest. Six methods scored on
the corpus ears, two seeds agreeing: uniform floor (range ~97, the pure random
walk), unigram/ngram1/ngram2/ngram3 all corpus-plausible (composite 0.79–0.82),
add-k smoothing a decisive negative (range 14→38 as it re-injects the rare
big-leap tail). Two takeaways worth keeping: context earns its keep through
*range control*, not interval fit; and the composite *saturates* across orders,
which is the first hard evidence that #9 (a higher-order self-similarity screen)
is the real next discriminator. bake_off.py is corpus-parametric (`--tokens`),
so it's ready for the folk corpora the moment their token files exist.

#3a Nottingham ingest is download-gated: fetching the GPLv3 repo is a network
download an unattended run can't authorize — queued in REVIEW as a one-line
operational unblock. Nothing else is blocked on it.

Run-5 highlights (dsp): cleared the three top non-gated items. The v6 curve
patches are no longer a UI trap — load/save/playback all preserve curves now
(headless `--roundtrip` mode proves it). The novelty metric is live and
self-verifying, which unblocked the first recursive-expansion sweep: 18
ExpandRule regimes ranked by timbral distance, with two rule-breakers
(golden-ratio and π semitone spacing) landing in the top third — those are the
"accidental discovery" bets for your ears. recurse 2-4 stays parked behind the
additive-performance work (item 8).

Next "go": comp = #4c neural next-note model (LLM-like predictor, numpy present)
as the next bake-off entrant → #9 higher-order self-similarity screen (now
motivated by concrete saturation evidence); #3a stays gated on the fetch
unblock; dsp = item 6 (FormantSequence deep-dive, now novelty-gatable),
then item 7 (oversampled FM) or item 8 (additive perf — also the gate for
recurse 2-4 expansion). Three dsp listen items now waiting (expand-sweep
survivors, CMA-ES stage-d, v6 curve ladder). Verdicts fold in whenever you
send them.

How this works: [WORKFLOW.md](WORKFLOW.md)
