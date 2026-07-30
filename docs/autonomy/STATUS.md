# Status — open this file first

Updated: 2026-07-30 — comp run 8 (Wolfie, scheduled) · dsp run 7 (Dipsy)

| Lane | Dev | Latest run | Backlog top | Awaiting your review |
|---|---|---|---|---|
| comp | Wolfie | run 8 (2026-07-30, scheduled): 4 fronts. Contrast-aware fig B (#2) — B sampled in relation to A (closure + rhythm kinship + monotony veto); range 12.6→9.1, zero 0.278→0.251, composite a wash by design → audition. Neural next-note (#3, G1) — numpy Bengio LM, val_ppl 28.5, soft-imported into bake-off; competitive-not-superior vs n-grams on MTD+Nottingham. #9 v2 self-similarity screen + --csv fix (#5). figure_transforms.py library (Matt's fragment). Essen #4 found data-gated. | wire transforms into phrase-builder (#10), then passage-strategy expansion (#6) | **2 listen + 1 unblock** — repeat-contour/varytail phrases (#2), contrast-B A/B (NEW), Essen fetch (OP-1) |
| dsp | Dipsy | run 7 (2026-07-29): see dsp/reports/2026-07-29-dipsy-run7.md | item 7 (oversampled FM) / item 8 (additive perf) — carried | **4 listen items** — formantseq survivors, expand-sweep survivors, CMA-ES stage-d A/B, v6 curve ladder |

Reports: comp/reports/2026-07-30-wolfie-run8.md ·
dsp/reports/2026-07-29-dipsy-run7.md

Run-8 highlights (comp): four fronts, all metric/build, no blind taste
iteration. (1) Contrast-aware fig B (#2) closed the last "contrast = TODO" in
the combination layer — B is now sampled in relation to A on an
antecedent/consequent closure objective; the measurable win is range control
(12.6→9.1 semitones, runaways killed) and the audition question (does the A/B
balance read musical?) is queued, not guessed. (2) The neural next-note model
(#3, Matt G1) is in the bake-off — a numpy Bengio LM, competitive with but not
superior to count-based backoff on the first-order plausibility metric, which is
itself the honest finding (the composite saturates; a learned representation
doesn't beat backoff at this corpus scale). (3) #9 v2 self-similarity screen now
catches motif-level "one figure hammered N×" that the first-order screens miss.
(4) figure_transforms.py is the reusable substrate for Matt's spec-1/2/3 repeat
variety. Matt's non-literal-repeat verdict was already answered by run-7's three
families (markov_phrases2/) — mapped explicitly in REVIEW, awaiting ears.

Essen (#4) is DATA-gated: the folksong corpus isn't on disk (corpus/kern is 10
classical pieces). One-line fetch unblock queued as REVIEW OP-1.

Next "go": comp = wire figure transforms into the phrase-builder (#10, uses the
run-8 library so repeated A occurrences can be transposed AND transform-varied at
full generality) → PassageStrategy expansion (#6, Matt re-raised). Essen stays
gated on the fetch. Two comp listen items + one unblock now waiting. Verdicts
fold in whenever you send them.

How this works: [WORKFLOW.md](WORKFLOW.md)
