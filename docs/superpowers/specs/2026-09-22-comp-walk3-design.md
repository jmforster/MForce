# Comp walk round 3 — genre profile as data + nested best-of-N

Date: 2026-09-22. Status: DRAFT, awaiting Matt's review of this file.
Brainstormed with Matt the same day (conversation after the walk2
annotations). Same stop condition as walk1/walk2: the lane doesn't move
off the ten Mary seeds until all ten are perfect ("perfect" = legal and
idiomatic NRS, not literally Mary). Harmony-first as now; the
melody-first test is deferred (backlog 25).

## 0. What changes, in plain terms

Three things. Everything else stays: the template, the figure slots, the
chord timeline, chain enumeration, and hard rules R1–R4.

1. **The rules become a genre file.** The per-note judgments now written
   as hand-tuned constants in anchor_selector.h (+3 / −3 / −4 ...) move
   into `styles/nursery_v1.json`. The numbers there are **odds you can
   read**: "the 7th of V7 steps down 96 times in 100". The engine uses the
   log of each odd, so what's written is what you get. Today's +3/−3 means
   ~400:1, not the 96:4 its comment claims. A different genre is a
   different file.
2. **A phrase critic.** The complaints that are about the phrase as a
   whole, not a single note, get their own scorer: stacked departures
   (a budget), range, repetition, motion. Some of today's terms already
   are whole-phrase (gap-fill, leap timing, final-note regression,
   penultimate) and move there.
3. **Best-of-N, nested.** Today the figure derivations (rep3_a, rep3_b,
   head_a) are rolled once per piece, and the anchor selector has to live
   with whatever it got. Now each phrase is built 10 times with fresh
   derivation rolls. The candidates are ranked and the dice pick among
   the top 3. Then the whole passage is built 10 times, ranked, and the
   dice pick among the top 3 again.

Plus one small fix: vary_steps may now change a figure's final step,
which makes Mary's exact head_a (D D E D) reachable.

## 1. The note map — `styles/nursery_v1.json`, `"melody"` block

### 1.1 Events, not notes

Consecutive repeated pitches merge into one **pitch event** for the
purpose of approach/departure (Matt: "F F F / F E E" is one F held, then
resolved). An event can span a chord change. That's how a suspension is
recognized.

### 1.2 Tendency rows (chord tones with somewhere to go)

Keyed `"<chord>:<member>"`, where chord = the Roman label of the active
chord at the event's end (`ChordLabel::to_string`, e.g. `V7`) and
member = 1/3/5/7. A row gives relative odds over the **departure motion**
from that event: `step_up`, `step_down`, `third_up`, `third_down`,
`leap_up`, `leap_down` (> a third), and `other` for anything unlisted.
Chord tones without a row are free (no term).

NRS v1 rows (authored starting values; Matt edits the file):

| Row | Odds | Source |
|---|---|---|
| `V7:7` (F over G7) | step_down 96, other 4 | walk1 s101, amendment 4 |
| `V7:3` / `V:3` (leading tone B) | step_up 88, third_down 7, third_up 4, other 1 | "3 of V → 1 of I 90%, 1 of V 10% (B G A B \| C)"; "B wants C, can leap to D first; leaping down weird" |

Contribution to a chain's score: `ln(odds / row_total)` for the motion
actually taken.

### 1.3 Non-chord-tone licenses

Every pitch event that is a non-chord tone of the chord sounding at its
onset (or, for suspensions, of the chord it rings into) is classified by
approach and departure:

| Type | Approach | Departure |
|---|---|---|
| passing | step | step, same direction |
| neighbor | step | step, opposite direction |
| suspension | held/repeated in from a chord where it WAS a chord tone | step down |
| unresolved suspension | same | anything else |
| anticipation | step | held/repeated into the next chord, where it is a chord tone |
| appoggiatura | leap | step |
| escape | step | leap |
| free | leap | leap |

Approach is judged against the previous pitch event even when it's in an
earlier phrase, because the passage-so-far is visible. Two modifiers: **accented** (onset on the bar downbeat) and **long**
(duration ≥ 2 beats; replaces today's longNct term).

Chord extensions (9th/11th/13th) aren't a separate class. They're
non-chord tones and get classified like any other. That reproduces Matt's
walk2 verdicts: Mary's D D **E** D over G7 is an upper neighbor
(licensed); D D D **E** C is an escape tone (not).

NRS v1 odds, per 100, with a chord tone = 100:

| Type | Odds |
|---|---|
| passing, neighbor | 100 |
| suspension | 50 |
| anticipation | 30 |
| unresolved suspension | 2 |
| appoggiatura, escape | 3 |
| free | 1 |
| accented modifier | × 0.3 (not applied to suspensions, which are accented by nature) |
| long modifier | → 1 |

Contribution: `ln(odds / 100)`.

### 1.4 Placement (kept as-is, moved to data)

Today's chord-tone placement bonuses are preferences, not event odds,
and stay additive log-scores: `"placement": {"chordTone": 1,
"barFinal": 1, "figureFinal": 1, "long": 1}` (today's values).

### 1.5 Departures

Any tendency motion or NCT license with odds **< 25** counts as a
**departure**, and the log names it (bar, beat, pitch, type, odds).
Threshold in the profile (`"departureBelow": 25`).

## 2. The phrase critic

Scores a complete phrase (a chain, in anchor-selector terms) with the
whole passage-so-far visible. Weights are in the profile's `"critic"`
block.

- **Departure budget** (fork (a), Matt's go 2026-09-22): NRS v1 allows
  1 per phrase. Over-budget candidates always rank below in-budget ones,
  at every selection level. They are never thrown out, so no dead ends:
  if nothing fits the budget, the best over-budget candidate still wins,
  and the log says so.
- **Range**: phrase span > 7 scale steps costs 1.5 per extra step (walk1
  s107's two octaves, walk2 s109's G3). Mary spans 4.
- **Repetition across harmony**: today's term, unchanged (same pitch
  > 4 beats across a chord change, −3; the seven Es).
- **Motion** (light, provisional): if more than half of the note-to-note
  moves are repeats, −4 × (fraction − 0.5). Mary is 0.42, so zero;
  walk1 s100's phrase 1 is 0.58, so −0.33. Weak by design: Matt called
  s100 "fine, no rules violated".
- **Moved from anchor_selector, unchanged formulas**: gap-fill +1.5,
  leap timing (0.25 / 0.5 / 1.0 per connector step by passage position),
  final-note regression (−0.75 per step beyond slack 2), penultimate =
  final −1.5.

**Not in walk3:** relation terms (echo/contrast with earlier phrases).
For Mary the template already guarantees the echo (parallel = same
start pitch + same `head` motif). Relation terms arrive with the first
template that needs them.

## 3. Nested best-of-N

A chain's total score = tendency + NCT licenses + placement + critic.

**Chain level** (inside `select_anchors`, as now): enumerate every legal
chain, then run a seeded roulette with weight exp(score), restricted to
in-budget chains when any exist.

**Phrase level** (new): N = 10 candidates. Each candidate:
1. Re-derives the derived motifs this phrase references **for the first
   time**, with a fresh candidate seed. Motifs pinned by
   `generationSeed` are never rerolled.
2. Runs the chain level on the result.

Candidates are ranked (in-budget first, then score). The top k = 3 are
rouletted by exp(score). The winning derivations are **committed**:
later phrases that reference the same motif reuse them. (In Mary:
phrase 1 owns rep3_a; phrase 2 owns rep3_b and head_a; head and rep3 are
authored.)

**Passage level** (new): M = 10 whole passages, each built as above.
Passage score = sum of phrase scores + passage range (same rule as §2,
over the whole passage). Rank, top 3, roulette. For Mary this decides
little; it exists because Matt asked for both levels, and gains terms
when passage-level judgments appear.

N, k and M live in the profile (`"search": {"phraseCandidates": 10,
"topK": 3, "passageCandidates": 10}`). Every draw is seeded from
masterSeed, so results are deterministic per seed. Cost: about 300k chain
scorings per render, well under a second.

## 4. vary_steps final step

`vary_steps` picks its index from `[1, n−1]` instead of `[1, n−2]`.
Step 0 stays excluded (dummy by convention). The walk template's head_a
goes to `transformParam: 3`: Mary's head_a needs three perturbations
(+1, +2, −2), all within ±2. "Scramble" (permute steps 1..n−1) stays
queued, not built.

Other users of vary_steps change output, deliberately (no back-compat,
per standing rule). In particular the crawl template
(`scores/baselines/template_mary.json`, a vary_steps transform on fig2)
must still render exact Mary. If the new index range moves it, re-pin
its seed in the data. No code shim.

## 5. Wiring

- Template: melody passage gains `"melodyProfile": "nursery_v1"`
  (resolved from styles/, like `styleName`). Harmonic anchor mode
  **requires** a profile and errors by name without one. The old
  constants are deleted, not kept as a fallback. The only harmonic-mode
  templates are the walk ones.
- `styles/nursery_v1.json` also carries Matt's chord map as
  `"transitions"` (I→IV, I→V, IV→V, IV→I, V→I), unused this round, for
  backlog 25. StyleTable must keep parsing a file that has a `"melody"`
  key.
- Decision log (`MFORCE_ANCHOR_LOG`), extended to cover each level:
  - each phrase candidate's derived figures (step lists)
  - the chosen chain
  - the score by category
  - every departure, named
  - the ranking and the draw
  - the passage-level ranking and draw

  This is the cause-and-effect Matt keeps asking for.

## 6. Gates and the batch

- **engine_tests**:
  - NCT classifier: one case per type, plus accented and long, including
    Mary's D D E D (neighbor) vs D D D E C (escape) and walk1 s101's F
    F F / F E E (suspension)
  - tendency lookup: odds → log contributions (the 96:4 row yields a
    24:1 ratio)
  - the budget ordering
  - reroll commit semantics (first-reference ownership, generationSeed
    pinned)
  - vary_steps reaching exact head_a from head for some seed
  - determinism per seed
- **Null gate**: byte-identical except the templates that use vary_steps
  or harmonic mode. The build lists each changed one, and the crawl
  still renders exact Mary.
- **Batch**: 10 seeds (masterSeed 100–109) → `renders/comp/audition/walk3/`
  - validator: R1–R4 plus the walk2 rhythm self-checks, 10/10
  - departures per phrase reported per seed; over-budget phrases called
    out in the README
  - passage strings to `docs/matt/Comp_walk3_for_annotation.txt`
    (standing order)
  - a REVIEW entry

## 7. Out of scope, recorded

- Figure-fit rules beyond the start (peak/nadir chord tones, unfittable
  policy): backlog 24, a future round.
- Melody-first harmonization test: backlog 25, after the ten seeds pass.
- Context priming ("less jarring after repeated Gs"): deferred.
- Compound-melody exception (B leaping down is fine if high C follows):
  a future profile level.
- Relation terms, passage-level taste beyond range.
- Scramble transform.
