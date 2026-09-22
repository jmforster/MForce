# Comp walk round 2 — the annotation ruleset

Date: 2026-09-21. Status: approved in brainstorm (Matt's annotations,
docs/matt/Comp_walks_annotated.txt, + 4 amendments same evening).
Iteration under the walk1 stop condition: the lane does not move off the
ten seeds until all ten are perfect. This spec embeds its own task plan
(§6) — an iteration round, not a new campaign.

## 1. Sources

- Matt's per-sibling annotations (primary data, committed at
  docs/matt/Comp_walks_annotated.txt) + the global transform verdict
  (REVIEW 21 fold).
- Amendments: (1) elaboration depth is an OPERATOR property, not a genre
  gate — sub-half-duration splits ~20%, and of those ~80% are the dotted
  pattern (e.+s for a quarter); (4) seventh-of-V resolves down RARELY not
  never (96:4); (9) register memory applies to the CADENTIAL TARGET only
  — leaps are encouraged, earlier in the piece; (10) gap-fill weighting
  confirmed ("10 upvotes"; Meyer's theory, empirically ≈ post-skip
  regression to the mean — same behavior either way).

## 2. Elaboration operator rework (figure_transforms::apply, Complexify)

complexify's uniform-random placement produced first-half sixteenth
clusters and fast repeated notes — every annotated rhythm smell. New
behavior (same TransformOp, same param = target note count; walk1's
renders change deliberately):

- **Ladder order**: while the figure still has a unit ≥ 2 beats, the
  first elaboration splits IT evenly (Mary's own h→q q move). Further
  elaborations pick positions weighted toward the BAR'S END (linear ramp
  on unit start time) — speed-up leads into the concluding bit.
- **Additive, no brakes**: splits only; never lengthen anything.
- **Depth policy** (amendment 1, genre-agnostic): a split produces
  sub-half-of-parent pieces only ~20% of the time. Of those fine splits,
  ~80% are the dotted pattern (parent q → e. + s); the rest even. The
  default split is always even halves.
- **Fast notes move** (annotation rule: repeated note in a fast run =
  computer): a split whose pieces are < 1 beat gives the new piece(s)
  NEIGHBOR/PASSING motion (as add_neighbor does) instead of step-0
  repeats. Sub-beat repeated pitches are never emitted by elaboration.

## 3. Anchor selector — sequential scoring terms (anchor_selector.h)

Existing per-note chord-tone scoring stays. New terms, computed on the
chain's realized (grid, beat, duration) track:

- **Seventh resolution** (96:4, amendment 4): a note whose pitch class is
  the active chord's 7th, followed by anything: next-note = one step DOWN
  earns +3; next-note ABOVE it costs −3. (s101's F-over-G7 rising to G.)
- **Suspension discipline**: a note sounding across a chord change that
  is a non-chord tone of the NEW chord must resolve down by step: +2 when
  it does, −4 when it doesn't. (Why s104's F→E sounded right.)
- **Long notes police extensions** (near-hard): a note with duration ≥
  2 beats that is a non-chord tone at its own beat costs −4 (the 13th/9th
  cases: E and A sat on G7 as long/landing notes).
- **Repetition across harmony** : the same grid repeated over a chord
  boundary for > 4 beats total costs −3 (the seven Es).
- **Penultimate ≠ final** (light): last two notes of the passage on the
  same grid cost −1.5 ("anticipating the final note — fine, not nursery").
- **Gap-fill** (positive): a leap (|Δgrid| ≥ 3) followed by stepwise
  motion (|Δ| ≤ 2) in the OPPOSITE direction earns +1.5 (s108's sublime
  C-down-to-F answered by A-G-G).
- **Leap timing** (amendment 9's flip side): the leadStep proximity
  penalty is halved (0.25/step) in the passage's first half and doubled
  (1.0/step) in its final quarter — leaps encouraged early, calmer late.

**Register memory — the one new HARD rule (amendment 9, cadential target
only):** the PASSAGE-final note must be a pitch (exact note number) the
melody has already visited — earlier phrases or earlier in the final
chain. Guard: when the visited set is empty (degenerate one-note
passages), the rule is skipped. This kills s103's never-heard high C
while leaving every leap before the cadence free.

Plumbing: compose_passage passes the visited note-number set accumulated
from previously composed phrases; select_anchors adds the current chain's
own pre-final notes.

## 4. Cause and effect — the decision log

Matt: "we want to know cause and effect." When the environment variable
`MFORCE_ANCHOR_LOG` is set (any value), select_anchors prints to stderr,
per phrase: the chosen chain (start pitch, leads, total score) with its
per-term score breakdown, plus the top 3 runners-up, plus counts of
chains eliminated per hard rule. The batch script captures this next to
each render (`mary_walk_s<seed>.log`). No output when unset — zero cost
to normal renders.

## 5. Batch, gates, queue

- Re-render the SAME ten masterSeeds (100–109) to
  renders/comp/audition/walk2/ (round-1 audio stays put — the
  annotations point at it). Emit passage strings
  (walk2_passages.txt) alongside, same vocabulary.
- Mechanical self-checks before queueing, all ten: R1–R3 validator
  (as walk1); NEW: no duration < 0.25 except none (no sub-sixteenth), no
  sub-beat repeated pitches, no sixteenth in a bar without eighths
  (should be near-vacuous under the new depth policy — checked anyway);
  passage-final pitch ∈ visited set.
- engine_tests: elaboration ladder stats (sub-half fraction ≈ 20% ± 10pp
  over 400 draws; dotted dominance among fine splits; late-placement
  bias; long-note-first rung; no fast repeats); selector terms (7th
  resolution preference across seeds; long-NCT near-never beats a CT
  alternative; register-memory hard case; gap-fill bonus measurable;
  empty-visited guard).
- Template null gate: expect EXACTLY ONE diff — template_mary_walk
  (deliberate: complexify + selector changed under it). The other 19
  byte-identical. mary_crawl / mary_structural must NOT move (no
  anchorMode, no derived motifs).
- REVIEW: walk2 entry supersedes the listening half of 21 (21's
  annotations remain the reference); question = "same ten seeds, your
  ruleset applied — score each against your own annotations."

## 6. Task plan (embedded; inline execution)

1. figure_transforms: rework Complexify per §2 (+ tests) — commit.
2. anchor_selector: §3 terms + visited-set plumbing + §4 log (+ tests) —
   commit.
3. Batch script: walk2 output dir, new self-checks, log capture; run;
   null gate; passage strings; REVIEW + STATUS/BACKLOG fold — commit(s).

## 7. Recorded, not in scope

- head_a's proper fix (permutation-class transform — Matt's original
  "rearranged" intent). Deliberately deferred one round: the rhythm and
  anchor rules land first so head_a's melodic failures can be judged
  without rhythm noise. Next round's candidate alongside re-shape-to-fit.
- Interior NCT policing beyond the specific terms above (suspension/
  long-note/7th) — the general hole stays open by choice.
- "Why" answers for GOOD breaks (s108) — the decision log now provides
  them for free.
