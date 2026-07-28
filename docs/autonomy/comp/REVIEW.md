# Composition lane — review queue

## Awaiting Matt

### 1. Parallel-period fix A/B [listen]
renders/wolfie_par_base_1.wav (consequent from running pitch, old behavior)
vs renders/wolfie_par_fix_1.wav (consequent restarts at C4 via new
"parallel": true flag). Verdict decides: whether parallel restart becomes
the default recommendation for period-style passages in future templates.

### 2. Scorer sanity [read/listen]
corpus/mtd_seg/score_generated.py composite scores: corpus anchors
0.86-0.90, k467-harmony 0.77, tiny phrase test 0.72. When you next listen
to generated pieces, note whether the composite ordering roughly tracks
your gut ranking — that calibrates the filter thresholds.
UPDATE (run 3): the Phase-2 outliers (below) exposed a known hole — the
repetitiveness screen is a one-sided floor, so a figure repeated 6× scores
0.74-0.84, as high as the good phrases. Backlog #9 fixes it (rep ceiling +
range weight). Your listen on item 3 will tell us whether the FIXED
composite ordering tracks your ear; verdict here decides the v2 thresholds.

### 3. Phase-2 combination phrases — do they sound musical? [listen]
renders/markov_phrases/ = 16 guarded 2-figure combinations (Markov atoms x
pattern x transform x placement, range-capped to corpus [7,19]).
renders/markov_phrases/outliers/ = 6 deliberate norm-breakers (over-long
repeats, uncapped range to 37) — the failure end of the axis.
Composites in renders/markov_phrases/scores.csv; top guarded phrases
(p05/p06 AAA'B) score 0.83-0.865. Verdict decides: (a) is the combination
layer producing musical phrases or still "computer music," and (b) does the
range guard's default cap (19) feel right or too tight. This is the Phase-2
"does it work" gate; a yes promotes contrast-figure work (fig B currently
sampled independent of fig A — the standing TODO) as the next combination
front.

## Resolved

(empty)
