# Comp walk round 1 — derived motifs + harmonic anchors

Date: 2026-09-21. Status: APPROVED IN BRAINSTORM (Matt, same day, verbatim
"spec/plan/build without me"); expectations set: "venture and probably
fall down." Campaign: comp backlog 23, walk stage; follows the crawl
(2026-09-21-comp-crawl-mary-design.md) and the structural template round.

## 1. What this round is

Two generative mechanisms replace authored data in the Mary template, at
once:

1. **Derived motifs**: only the two atoms (`head`, `rep3`) and the arrival
   (`close`) carry authored units. `rep3_a`, `rep3_b`, `head_a` are
   DECLARED as derivations (`derivedFrom` + `transform` + param) and
   synthesized at compose time — Matt's stated grammar: "head is generated
   from whole cloth, as is rep3, everything after that is
   FigureTransform'ed versions of the predecessors."
2. **Harmonic anchors**: authored connectors and phrase-start pitches are
   jettisoned. Each figure's starting degree is SELECTED against the chord
   timeline under Matt's rules and weights (§4). "The rules govern the
   selection of scale degree, which ultimately is a good or bad choice
   based on the chord and the shape of the upcoming figure."

Figure SLOTS stay authored (which motif goes where — head, rep3, rep3,
rep3_a / head, rep3_b, head_a, close), as does the parallel-period intent.
Fully generated figures are explicitly NOT this round (Matt: "I wasn't
going to jump into fully generated figures yet").

**The bar**: a seeded batch over the Mary harmony where every render is a
legal nursery tune. Exact Mary is NOT required to be in the support —
`vary_steps` cannot alter a figure's final step (interior-only), so
`head_a`'s exact form is unreachable; recorded as accepted (§7). Matt:
"we don't care if it changes E E E E to E E-F E D-C."

## 2. Derivation model

- A motif whose JSON declares `derivedFrom` (+ `transform`, optional
  `transformParam`, optional `generationSeed`) and carries NO content is
  synthesized during `realize_motifs`: resolve the parent's REALIZED
  figure (chains allowed — parents resolve first; a cycle or missing
  parent throws by name), then apply `figure_transforms::apply(parent,
  op, param, seed)` with seed = `generationSeed` if set, else drawn from
  the compose rng (so `masterSeed` varies it per batch member).
- A derived motif that DOES carry content keeps it (idempotent reload of
  a saved template does not re-derive).
- `add_derived_motif` (templates.h) remains for programmatic use;
  `realize_motifs` uses `figure_transforms::apply` directly for uniform
  op coverage.

**New op — `TransformOp::Complexify`** (enum + JSON name `"complexify"` +
`figure_transforms::apply` case): elaborate via the existing
`complexify(fig, rng, amount)`, with **param = target unit count**
(Matt's "reined in via note count"); param 0 → one extra unit. amount is
derived: `param/base_count − 1`, clamped ≥ 0.

Mary's declarations:
- `rep3_a` = `{derivedFrom: rep3, transform: vary_steps, transformParam: 1}`
  (exact egg IS in support: one interior perturbation of +2).
- `rep3_b` = `{derivedFrom: rep3, transform: complexify, transformParam: 4}`
  (support includes exact rep4 — split the half — plus neighbor/turn
  elaborations Matt pre-approved).
- `head_a` = `{derivedFrom: head, transform: vary_steps, transformParam: 2}`
  ("rearranged and sorta inverted" — the exact bar-7 cell is out of
  support, accepted).

## 3. Harmonic anchor selection

New header `engine/include/mforce/music/anchor_selector.h`. Opt-in via a
new `PassageTemplate.anchorMode` field: `""`/`"authored"` (default, byte-
identical behavior) or `"harmonic"`.

Under `harmonic`, for each phrase in the passage (run inside
`DefaultPassageStrategy::compose_passage` after motif realization, before
`compose_phrase`):

- **Candidate space** (enumerate, never sample-and-reject): the phrase's
  anchor chain = start degree A₀ plus a leadStep per subsequent figure.
  A₀ ranges over chord tones of the phrase-opening chord within ±4 scale
  steps of the REGISTER ANCHOR (the authored passage/phrase
  `startingPitch`, which under harmonic mode means register, not pitch);
  each leadStep ranges over [−4, +4]. Figures' realized note tracks
  (degree + beat) are computed from the chain via the realized figures'
  step/duration lists.
- **Hard rules** (Matt, verbatim intent, 2026-09-21):
  - R1: the phrase-opening downbeat is a chord tone of the active chord.
    No weighting among root/3rd/5th.
  - R2: the last note of a phrase is a chord tone of its chord.
  - R3: the last note of the PASSAGE is scale degree 1. Period.
- **Soft score**, summed over the phrase's notes: a chord-tone note
  contributes `1 + isBarFinal + isFigureFinal + isLongerThanDefaultPulse`
  (Matt's thrice-boost, stacking; defaultPulse = the template's
  defaultPulse, falling back to 1 beat); a non-chord-tone note
  contributes 0. Minus `0.5 × Σ|leadStep|` (cursor-proximity /
  voice-leading preference).
- **Choice among legal chains**: seeded roulette with probability
  ∝ exp(score) over the legal set (temperature 1.0 to start). Determinism
  per masterSeed; variety across seeds. An empty legal set throws with
  the phrase name and rule that could not be met.
- **Parallel intent**: a phrase with `parallel: true` reuses the previous
  phrase's CHOSEN A₀ pitch outright (the literal-repeat opening is one
  pinned decision, per the brainstorm), and its remaining chain is
  selected normally.
- Output: the selector writes the phrase's `startingPitch` and dense
  connectors (`[null, ...]` convention) into the working PhraseTemplate;
  everything downstream (compose, realize) is unchanged.

Chord source: the composed Section's `harmonyTimeline` (already populated
from the template before parts compose), consulted at the running beat of
each note.

## 4. Template v4 and the batch

`scores/baselines/template_mary_walk.json`: Mary harmony block (unchanged),
motifs = head/rep3/close authored + the three declarations of §2, melody
passage `anchorMode: "harmonic"`, figure slots as in the structural
template, consequent `parallel: true`, NO connectors, `startingPitch` E5
retained as register anchor. Instruments per standing rule.

Batch: 10 renders, masterSeed 100+i, to `renders/comp/audition/walk1/`
(`mary_walk_s<seed>_1.wav`), README naming the question ("legal nursery
tunes? which siblings please you?"), REVIEW entry (next free number).
Mechanical self-check before queueing: a rules validator re-derives
R1/R2/R3 from each render's piece JSON + chord timeline and every render
must pass; any failure is a build bug, not queue material.

## 5. Gates

- engine_tests: Complexify dispatch (param = note count honored);
  derivation chains (parent-first, missing-parent/cycle throw, seeded
  determinism, content-bearing motif not re-derived); anchor selector
  unit cases (R1/R2/R3 enforced; a constructed case where the long
  figure-final boost flips the selection; parallel pinning; empty-legal
  throws).
- Template null gate byte-identical over the existing corpus (all new
  behavior is opt-in: absent anchorMode + content-bearing motifs change
  nothing).
- Zero-event check green (batch renders all have 26-34 melody events).
- The 10-render batch passes the R1-3 validator, 10/10.

## 6. Out of scope, recorded

- Generated figure SLOTS / atom generation (Markov proposes) — next stage.
- Interior non-chord-tone policing (the minor-Mary hole stands, accepted
  2026-09-21 brainstorm).
- Re-shape-to-fit (altering a figure to suit a kept anchor — Matt's
  C-B-A-G-C example) — the second fit operator, future round.
- Onset/articulation intents; rests in derivation.

## 7. Known limitations, stated up front

- Exact Mary is not in this round's support (head_a unreachable —
  vary_steps is interior-only). A future "final-step freedom" (cadential
  variation) thread is the fix; do not widen vary_steps silently.
- vary_rhythm never splits a figure's LAST unit, which is why rep3_b
  goes through Complexify instead.
- The anchor selector treats register octaves via the ±4-step window
  around the register anchor only; no melodic-range ceiling yet.
