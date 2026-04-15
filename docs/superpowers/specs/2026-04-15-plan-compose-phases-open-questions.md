# Plan / Compose Phases — Open Questions Parked for Later

## Purpose

This doc parks questions raised during the 2026-04-15 brainstorm of the plan/compose strategy interface that we've deferred past the initial spec cycle. When we get to implementation or a follow-up spec, this list is the starting point.

## Question 1: Orphan motifs after regeneration

**Scenario:**
A user calls `plan_passage()` on a period-aware strategy. The strategy synthesizes and adds `Fig1_inv`, `Fig2_var`, `Fig3_cadence_adjusted` to the motif pool. The user then regenerates the passage (e.g., because they didn't like the result, or rolled a different `variant`). The strategy runs again, possibly produces a different set of additions — `Fig1_retro`, `Fig4_var`. The first run's additions (`Fig1_inv`, etc.) may now be unreferenced.

**Question:** how do we (should we) clean those up?

**Options:**

1. **Never clean up.** Pool accretes forever. User deletes manually via UI.
2. **Reference-diffing.** Before/after regen, compute the set of referenced motif names; remove orphans (those with `origin != User`) unless locked.
3. **Source-tagged contributions.** Each motif added by `plan_*` carries a `addedBy` metadata (e.g. `"Section1_Passage0_Phrase2"` or a stable planRunId). When a planner re-runs for that source, its prior contributions are removed first. User-authored motifs (origin=User) are never auto-removed. Locked motifs are never auto-removed regardless of origin.
4. **Snapshot + rollback.** Before regen, snapshot the pool; on regen success, diff and archive orphans; offer "restore" in UI.

**Lean:** option 3. It mirrors the locking model already in the codebase (`locked: bool` on FigureTemplate / PhraseTemplate / PassageTemplate), composes naturally with user locking, and makes the pool's growth traceable to specific compositional decisions.

**Defer until:** the UI workflow that triggers regeneration is defined. Until there's a concrete UI path, any auto-cleanup policy is speculative.

## Question 2: Cadential Motif shape — PulseSequence vs full MelodicFigure

**Context:**
Matt noted that `Cadential` motifs sometimes work well as pure `PulseSequence` (stereotyped rhythm, context-dependent contour) but other times need a full `MelodicFigure` — specifically flagging that the cadential approach in K467's second period (the PAC landing) may not work as rhythm-only.

**Question left open:** what makes a cadence *require* a baked-in contour vs permit a contour derived at use time?

Hypothesis (to test against repertoire, not assert here): contour-required cases are those with specific *contrapuntal* or *ornamental* constraints — e.g., a cadence with a built-in suspension, or a specific trill-into-resolution gesture. Rhythm-only cases are generic approach rhythms (dotted-quarter into quarters, running eighths into a long note).

**Defer until:** K467 bars 1-12 implementation reveals the concrete case. The PulseSequence-capable Cadential motif mechanism lands now (the Motif content variant already supports it); what we defer is the heuristic for when a strategy *picks* a rhythm-only cadential vs a full-figure cadential.

## Question 3: Plan phase for Figure and Phrase strategies

**Context:**
We agreed the Strategy interface gains a two-phase shape (`plan_*` + `compose_*`) at the Passage level. The user's worked example (K467 planning via `PeriodPassageStrategy`) builds all PhraseTemplates directly from inside the Passage strategy's plan.

**Question:** do Figure and Phrase strategies also have `plan_*` methods, or is planning a Passage-level concern only?

**Options:**

- **(a) Plan at every level.** `FigureStrategy::plan_figure`, `PhraseStrategy::plan_phrase`, `PassageStrategy::plan_passage`. Planning can recurse: `plan_passage` can call `plan_phrase` for each phrase, etc. Maximum flexibility.
- **(b) Plan at Passage only.** Passage-level plan constructs all nested templates directly. Figure and Phrase strategies have only `compose_*`. Simpler, matches the K467 worked example.
- **(c) Plan wherever the strategy opts in.** Optional method with a default no-op implementation that returns the seed unchanged. Individual strategies override as needed.

**Lean:** (c). Matches C++ virtual-method idiom, doesn't force meaningless plan_* implementations on strategies that have nothing to plan (most ShapeStrategies are pure figure generators given a template).

**Defer until:** writing the plan/compose split spec itself. The answer affects interface shape and deserves its own treatment.

## Question 4: Motif pool API shape — atomic `add_motif` vs batched

**Context:**
If `plan_passage` synthesizes multiple motifs, does it call `add_motif` once per motif (incremental) or build a local batch and commit at the end (transactional)?

**Tradeoffs:** incremental is simpler; transactional permits "planning failed, roll back" cleanly.

**Lean:** incremental. Plan failure is rare in deterministic code paths; rollback semantics add complexity without a concrete need. If we ever need transactional plan, a `PoolTransaction` RAII helper can be added non-invasively.

**Defer until:** an actual rollback scenario emerges.

## Question 5: Realized content embedding in templates

**Context:**
Matt clarified: after `plan_*`, the returned template should be **self-contained** — compose() reads only the template, not the pool.

**Question left open:** exactly which existing template field holds the resolved motif content?

Existing shape: `FigureTemplate` has `std::optional<MelodicFigure> lockedFigure;` plus `FigureSource::Locked`. The plan phase's resolved output naturally lands there — `lockedFigure = *pool.find_motif(name)`, `source = FigureSource::Locked`. But:

- PhraseTemplate has no equivalent "locked phrase" pre-baked field.
- PassageTemplate has no equivalent.

**Options:**

- **(a) Extend the `locked*` pattern to all template levels.** Consistent; possibly over-engineered.
- **(b) Rely on `FigureTemplate::lockedFigure` alone.** A PhraseTemplate is locked when all its FigureTemplates are locked. PassageTemplate analogously. Simpler but requires a walk to answer "is this phrase fully resolved?".
- **(c) Separate concept.** Introduce a `resolved` flag distinct from `locked` — resolved means "plan() has filled this in", locked means "user pinned this, don't regenerate".

**Lean:** (b), maybe augmented by a derived `is_resolved()` helper on PhraseTemplate / PassageTemplate. `locked` stays the user-level intent; plan() uses it transparently — planning an already-locked template is a no-op.

**Defer until:** the plan/compose split spec. Tied to question 3.

## Status

None of these are blockers for writing the full plan/compose split spec. They're annotations to pick up when the answer becomes concrete rather than speculative.
