# Composer Strategy API Refactor — Design

## Context

The composer has accreted a `StrategyContext` (`engine/include/mforce/music/strategy.h:20`) that now carries 11 fields, threaded through every `realize_figure` / `realize_phrase` / `realize_passage` call. An audit of the live compose path (the path that produces the K467 opening four bars and bars 1–27) shows that `StrategyContext` has become the bag-o-stuff it was never designed to be, and that its underlying threading assumptions — left-to-right running state, composer back-pointer for dispatch, RNG plumbing through ctx — are either unused, unnecessary, or actively wrong for the user-facing workflow of locking and regenerating specific phrases out of order.

This refactor deletes `StrategyContext`, shrinks `realize_*` signatures to two arguments, and moves everything previously on ctx to one of three destinations: (1) derivable from `Piece` + structural position, (2) a process-wide singleton, (3) deleted as dead code.

## Problem

### StrategyContext audit

Field-by-field audit of `StrategyContext` reads in the live compose path (strategies, figure generators, shape strategies, the AFS passage strategy, cadence application):

| Field | Reads on `ctx.X` | Verdict |
|-------|-------|---------|
| `scale` | ~15 reads (PitchReader ctor, degree math, cadence, `chord.resolve`) | **Used**, but denormalized cache of `section->keyContext`; derivable. |
| `cursor` | Read + mutated at every level | **Used + mutated** — the only genuinely running-state field. |
| `totalBeats` | **0 reads** | **Dead.** All `totalBeats` reads are on `ft.totalBeats` / `pt.totalBeats`. |
| `piece` | **0 reads** (two writes in composer.h:295, 330) | **Dead.** |
| `template_` | **0 reads** (two writes) | **Dead.** |
| `composer` | Read at every level — recursion, motif lookup, registry lookup | **Used**, but only because dispatch and services were wired through it. |
| `params` | **0 reads** | **Dead.** Strategy params live on template fields. |
| `rng` | Read everywhere — every shape strategy, every figure generator | **Used.** |
| `chordProgression` | 3 reads, all in `AlternatingFigureStrategy::realize_passage` | **Used**, but reachable via `piece→section->chordProgression`. |
| `keyContexts` | **0 reads** | **Dead.** |
| `sectionBeatOffset` | **0 reads** | **Dead.** |

**6 of 11 fields are literal dead code.** Of the 5 survivors: `scale` and `chordProgression` are denormalized caches; `composer` is dispatch infrastructure; `rng` is a shared singleton in disguise; `cursor` is the only truly running state.

### Running-state threading is wrong for the user workflow

The current cursor model threads a single `Pitch cursor` left-to-right through the passage: passage strategy sets it, each phrase strategy mutates it, each figure strategy's output advances it. This works for sequential composition but breaks for the core user-facing workflow — **step-by-step generation with accept/reject on individual phrases**. If the user locks Phrase 1 and Phrase 3 and asks to regenerate only Phrase 2, the cursor going into Phrase 2 is determined by Phrase 1's realized tail *at the moment of the query*, not by any value threaded through a prior compose pass. Left-to-right running state presupposes monotonic sequential composition; the real workflow is random-access regeneration.

### Composer-as-dispatch-broker obscures strategy decisions

Today, a `PassageStrategy` recurses into child phrases via `ctx.composer->realize_phrase(phraseTmpl, ctx)`, which performs a registry lookup keyed on `phraseTmpl.strategy` and delegates to whichever strategy is registered under that name. This means a `PassagePeriod` strategy cannot express "for phrase 0 I'm making an antecedent, for phrase 1 I'm making a consequent" — it just pushes a template through the registry and hopes. The compositional decision belongs *inside* the PassageStrategy, not in a generic broker.

## Goals

- **Shrink the surface area** between Composer and strategies to the minimum that actually carries information.
- **Make running state a query** against realized content, not a threaded accumulator, so random-access regeneration works.
- **Localize compositional decisions** inside the strategy that makes them: a passage strategy directly picks and invokes its child phrase strategies.
- **Preserve JSON-driven configurability** — templates can still name strategies by string; that resolution happens at load time.
- **No behavioral regression** — every current render (four-bar K467, bars 1–27 DURN, goldens) produces identical output after the refactor.

## Non-goals

- Richer `PhraseTemplate` / `PassageTemplate` fields (Workstream 3, separate spec).
- Richer motif pool (Workstream 1, separate spec).
- History accumulation beyond cursor (Workstream 2, separate spec).
- `ChordProgressionStrategy` as a new strategy level (Workstream 4, separate spec).
- First-class runtime Phrase metadata — explicitly rejected during brainstorm. Runtime `Phrase` stays as `startingPitch + figures[]`. Performer-relevant hints will flow via EventSequence markers when Workstream 4 adds them.

## Design

### New strategy interface

`StrategyContext` is deleted. Each strategy level gets its own typed base class with a two-argument `realize` method:

```cpp
class PassageStrategy {
public:
  virtual ~PassageStrategy() = default;
  virtual std::string name() const = 0;
  virtual Passage realize(Locus, const PassageTemplate&) = 0;
};

class PhraseStrategy {
public:
  virtual ~PhraseStrategy() = default;
  virtual std::string name() const = 0;
  virtual Phrase realize(Locus, const PhraseTemplate&) = 0;
};

class FigureStrategy {
public:
  virtual ~FigureStrategy() = default;
  virtual std::string name() const = 0;
  virtual MelodicFigure realize(Locus, const FigureTemplate&) = 0;
};
```

No shared abstract `Strategy` base with three `throw`ing virtual methods — that conflation goes away. The three levels are peers.

### Locus

```cpp
struct Locus {
  Piece& piece;               // authoritative structural + harmonic truth
  PieceTemplate& pieceTemplate; // authoritative compositional DNA (motif pool, templates)
  int sectionIdx;
  int partIdx;
  int passageIdx{-1};         // -1 = n/a at this level
  int phraseIdx{-1};
  int figureIdx{-1};
};
```

A `Locus` names a coordinate in the structural hierarchy and pairs it with the two canonical structures a strategy can need: the realized `Piece` and the driving `PieceTemplate`. Its only invariant is "where am I?"; it does not accrete other fields. All derived information flows from these two references plus the index path. Queries such as:

- Current `Scale` and `KeyContext`: `locus.piece.sections[sectionIdx].keyContextAt(...)`
- Current `ChordProgression`: `locus.piece.sections[sectionIdx].chordProgression`
- Motif pool: `locus.pieceTemplate.motifs` — motifs live on `PieceTemplate` (both declarations and realized forms; matches the existing `lockedFigure` caching pattern on `FigureTemplate`).
- Cursor / starting pitch for my position: `piece_utils::pitch_before(locus)` — computes from the last realized note preceding this `Locus`, or falls back to the passage's `startingPitch` if this is the first figure.
- History preceding my position: `piece_utils::realized_before(locus)` — returns a view of all realized notes preceding this `Locus`. This is the leverage point for Workstream 2.

Because these are pure functions of `Piece` + position, they are correct regardless of traversal order — supporting the regen-Phrase-2 workflow.

### Direct strategy-to-strategy dispatch

A `PassageStrategy` holds references to its chosen child `PhraseStrategy` instances (either constructed inline, or resolved from the registry at construction time) and invokes them directly:

```cpp
Passage PassagePeriodStrategy::realize(Locus locus, const PassageTemplate& pt) {
  Passage out;
  out.phrases.push_back(antecedent_.realize(locus.with_phrase(0), pt.phrases[0]));
  out.phrases.push_back(consequent_.realize(locus.with_phrase(1), pt.phrases[1]));
  return out;
}
```

No registry lookup from inside a strategy's body, no ctx pass-through. If `pt.phrases[i].strategy` is non-empty (user-overridden in JSON), the passage strategy MAY resolve it via the registry at construction time or at first invocation; but that resolution is the passage strategy's decision, not Composer-as-middleman.

### Registry as load-time factory

`StrategyRegistry` stays, but its role narrows to **serialization-time resolution**: when loading a `PieceTemplate` from JSON, each `template.strategy = "phrase_antecedent"` string is resolved once to a concrete `PhraseStrategy` instance, cached on the template (or on a side map keyed by template identity), and that instance is invoked thereafter. No `composer->registry_get(name)` lookups in hot paths.

Access pattern:

```cpp
PhraseStrategy* s = StrategyRegistry::instance().resolve_phrase(name);
```

Singleton, no threading. If we ever need testability (e.g., injecting a mock registry), we add a push/pop guard like we will for RNG.

### RNG as thread-local singleton

`Randomizer* ctx.rng` becomes a thread-local free function:

```cpp
namespace mforce::rng {
  uint32_t next();                       // equivalent to ctx.rng->rng()
  struct Scope {                         // RAII guard for tests / reproducibility
    explicit Scope(uint32_t seed);
    ~Scope();
  };
}
```

Composer installs a `rng::Scope` at the top of `compose()` with the piece seed; all strategies call `mforce::rng::next()` directly. Same determinism guarantees as today, minus the plumbing.

### Composer becomes a thin orchestrator

The `Composer` class loses its role as dispatch broker. Its remaining responsibilities:

1. Entry point: `compose(pieceTemplate) -> Piece` — walks top-level structure, installs RNG scope, resolves root-level strategies from registry, kicks off dispatch.
2. Owns the motif pool (until it moves to `Piece`; see Workstream 1).
3. Handles phrase/passage/figure locking decisions (checks `locked` / `lockedFigure` on templates).

It no longer holds a back-pointer that strategies reach through. `ctx.composer->find_rhythm_motif(...)` is replaced with `locus.pieceTemplate.motifs.find_rhythm(...)`.

## Migration plan

Staged so each step is independently validated against existing goldens and renders.

### Stage 0: freeze behavior

Establish the behavioral baseline. Re-render the representative set: K467 bars 1–4 (the four structural variants `test_k467_v1`..`v4`), K467 bars 1–27 DURN, the Phase-1b TriTest golden, the Period/Sentence smoke templates, and the harmony-first AFS K467 test. Record binary WAV hashes and pin the realized JSON. Any subsequent stage that changes these is a behavioral regression and must be investigated before proceeding.

### Stage 1: add `Locus` and query helpers, without changing signatures

Introduce `Locus` and `piece_utils::pitch_before(locus)` / `realized_before(locus)` as additive code. Wire them into a single non-critical read path (e.g., replace one `ctx.cursor` read in `DefaultPhraseStrategy::realize_phrase` with a `pitch_before(locus)` call, where `locus` is constructed on the spot). Verify goldens still match. This proves the query model produces the same answers as the threaded model in the sequential case.

### Stage 2: RNG singleton

Replace `ctx.rng->rng()` with `mforce::rng::next()` everywhere. Composer installs `rng::Scope(piece.seed)` before dispatch. Verify goldens match bit-for-bit (this one matters — the RNG draw sequence must be preserved exactly; any reordering breaks determinism).

### Stage 3: delete dead fields

Remove `totalBeats`, `piece`, `template_`, `params`, `keyContexts`, `sectionBeatOffset` from `StrategyContext`. They have zero reads in the compose path; deletion is purely cleanup. Verify goldens match.

### Stage 4: split `Strategy` base into three typed bases

Introduce `PassageStrategy`, `PhraseStrategy`, `FigureStrategy` as peers. Retrofit existing strategies to inherit from the appropriate one. The abstract `Strategy` base with its three `throw`ing virtual methods goes away. Registry gains three typed `resolve_*` methods. Verify goldens match.

### Stage 5: convert dispatch to direct invocation

Update `DefaultPassageStrategy`, `DefaultPhraseStrategy`, and `AlternatingFigureStrategy` to invoke their child strategies directly rather than through `ctx.composer->realize_phrase(...)` / `realize_figure(...)`. For this mechanical stage, to preserve current JSON-driven behavior, the child strategy is resolved from `StrategyRegistry::instance()` using the template's `strategy` field at the top of `realize()`, then invoked directly. Future strategies (Workstream 4) may bake their child choices in at construction time; that's additive and out of scope here. Verify goldens match.

### Stage 6: two-arg signature, delete `StrategyContext`

Replace `realize_*(template, ctx)` with `realize(Locus, template)`. Delete `StrategyContext`. Every call site updated. Cursor reads become `piece_utils::pitch_before(locus)`; chord-progression reads become `locus.piece.sections[locus.sectionIdx].chordProgression`; scale reads become `locus.piece.sections[locus.sectionIdx].keyContextAt(...).scale`. Verify goldens match.

Stages 1–6 are each independently revert-able. The refactor can stop after any stage if something unexpected surfaces.

## Resolved during brainstorm

- **Motif pool home.** `PieceTemplate`. Both declarations and realized forms live there, matching the existing `FigureTemplate::lockedFigure` caching pattern. Locus carries a `PieceTemplate&` alongside `Piece&` for access.
- **Cursor query cost.** Accepted. `pitch_before(locus)` walking prior realized figures is O(n) per figure / O(n²) total; composition is not a hot loop and the simplicity is worth it.
- **Locking + regen semantics.** Regenerating a locked phrase is a no-op — the request is silently ignored. The broader question of what happens to locked Phrase 3 when its predecessor Phrase 2 is regenerated with a new tail pitch is accepted as a known discontinuity for now; UX will settle it later. This refactor preserves current locking behavior.

## Open questions to settle during planning

- **Locus as bundle vs. explicit args.** `Locus` is a small struct; if it starts accreting (e.g., "temporary override scale"), we've recreated the bag. The invariant that keeps it honest: *Locus names a coordinate + pairs it with the two canonical structures (Piece, PieceTemplate), nothing else*. Enforce in review, not in the type system.

## What this enables

This refactor is a prerequisite for the four workstreams identified during brainstorming:

1. **Motif pool** — richer `Motif` metadata (function tags, chord-suitability, origin). Reachable via `locus.piece.motifs` regardless of where the pool lives.
2. **History accumulation** — `realized_before(locus)` is already a query; expanding it with domain-specific accessors ("last N pitches," "melodic contour since phrase start," "register range of preceding passage") is additive.
3. **Richer templates** — `PhraseTemplate` gains explicit role slots (opening / body / cadence / optional anacrusis) once we know what strategies want to read. Not coupled to plumbing.
4. **Better, well-named Strategies + ChordProgression strategies** — with direct dispatch, a new `PassagePeriod` or `PhraseAntecedent` strategy is just a class; `ChordProgressionStrategy` becomes a fourth peer to the existing three strategy levels and plugs into the registry the same way.

Each workstream gets its own brainstorm → spec → plan → implementation cycle, built on the new API. The refactor itself is purely mechanical and should not produce audible change.
