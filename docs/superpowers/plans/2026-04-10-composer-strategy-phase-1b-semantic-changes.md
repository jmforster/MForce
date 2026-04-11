# Composer Strategy — Phase 1b Semantic Changes Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Land the semantic changes that Phase 1a explicitly deferred — delete `FigureConnector` and the connector mechanism entirely; switch to a running-cursor model where each figure's first-unit step bridges from where the previous figure left the cursor; rename `StrategyContext::startingPitch` to `cursor`; require `startingPitch` on `PassageTemplate`; add a `literal` figure source for hand-authored note lists. Pin a new golden after manual audio audition.

**Architecture:** This is a semantic change, not a refactor. The composer output will differ by design. The regression check is a NEW golden hash captured after a human audio audition of the new output. Every existing template/corpus that uses connectors gets migrated (DURN parser absorbs connector steps into the following figure's first-unit step).

**Tech Stack:** C++17, nlohmann::json, CMake. No new dependencies. No test framework; regression is a hash of a re-rendered golden + human audition.

---

## Scope Guardrails

- **All existing semantic assumptions about connectors go away.** `FigureConnector`, `ConnectorType`, `Phrase::connectors`, `PhraseTemplate::connectors`, the default `step(-1)` insertion, the connector contribution in `apply_cadence`, and the connector switch in `Conductor::perform_phrase` are all deleted.
- **New cursor rule, uniformly applied.** At render time, every `FigureUnit.step` is applied to the cursor — **including the first unit of every figure**. Pre-Phase-1b behavior ignored the first unit's step (the figure was assumed to "start at" phrase.startingPitch + connector state). In Phase 1b, the first unit's step *is* the transition from the previous cursor position (or from the phrase's initial cursor, for the first figure) to the figure's first sounding note. When a figure's first step is `0` the behavior collapses to "figure starts at cursor" — which is the convention the existing codebase already uses for composer-generated figures, so most existing output stays reasonable.
- **`PassageTemplate::startingPitch` becomes REQUIRED.** The new initial-cursor rule needs a single authoritative source for where a passage begins. Templates without one fail to load — no silent default.
- **`PhraseTemplate::startingPitch` stays optional.** It's an override: if present, it resets the cursor at the start of that phrase. If absent, the cursor is inherited from wherever the previous phrase left it. For the first phrase of a passage, the absence-fallback is `PassageTemplate::startingPitch`.
- **`FigureTemplate` still has no `startingPitch`.** It never did, but Phase 1a's spec edit made the rule explicit. This plan does not add one.
- **The post-Phase-1b golden IS allowed to differ from the Phase 1a golden.** That's the point. Do not try to preserve the Phase 1a hash.
- **Do NOT touch `StepGenerator`, `FigureBuilder`, `PitchReader`, `Scale`, or any low-level music primitives.**
- **Do NOT revisit Phase 1a decisions.** The `seed + 200` offset on `Composer::rng_` stays. The Strategy/Registry/Composer structure stays. The three Default strategies stay.
- **Header-only implementations** for new code, matching the existing convention.

---

## File Structure

All changes land in existing files. No new headers. The files touched:

| File | Change |
|---|---|
| `engine/include/mforce/music/figures.h` | Delete `ConnectorType` enum and `FigureConnector` struct. |
| `engine/include/mforce/music/structure.h` | Delete `Phrase::connectors` field; delete the `add_figure(fig, connector)` overload on `Phrase`. |
| `engine/include/mforce/music/templates.h` | Delete `PhraseTemplate::connectors`; add `PassageTemplate::startingPitch` (required); add `FigureSource::Literal`; add a literal-notes field on `FigureTemplate`. |
| `engine/include/mforce/music/templates_json.h` | Drop connector serialization; add passage startingPitch serialization; add `"literal"` source + notes list parsing; validate that `PassageTemplate::startingPitch` is present when loading. |
| `engine/include/mforce/music/music_json.h` | Drop `ConnectorType`/`FigureConnector` serialization; drop `Phrase::connectors` serialization. |
| `engine/include/mforce/music/conductor.h` | Rewrite `perform_phrase` to walk figures with the new cursor rule (apply every unit's step, including the first). Delete the connector switch. |
| `engine/include/mforce/music/dun_parser.h` | On encountering an `Xn` connector token, absorb `n` into the next figure's first-unit step instead of emitting a `FigureConnector::step(n)`. |
| `engine/include/mforce/music/strategy.h` | Rename `StrategyContext::startingPitch` → `cursor`. |
| `engine/include/mforce/music/composer.h` | Update `DefaultPhraseStrategy::realize_phrase` (drop connector insertion), `DefaultPhraseStrategy::apply_cadence` (drop connector step summation), `DefaultPassageStrategy::realize_passage` (initial cursor from `PassageTemplate::startingPitch` — no more hardcoded `reader.set_pitch(5, 0)`), `DefaultFigureStrategy::realize_figure` (add `Literal` case), cursor inheritance between phrases. |
| `engine/include/mforce/music/default_strategies.h` | Update class declarations if signatures changed; `DefaultFigureStrategy::literal_to_figure` helper for the new source. |
| `tools/mforce_cli/main.cpp` | Only if any CLI code touches connectors directly (currently only `conductor.h` and `dun_parser.h` do — verify with grep and touch main.cpp only if grep finds a hit). |
| `patches/template_golden_phase1a.json` | Add `startingPitch` on the `Main` passage. This file is the golden template; the plan RE-PINS the hash at the end. |
| `renders/template_golden_phase1a.sha256` | Rewritten by Task 7 after audio audition. |
| `renders/template_golden_phase1a.wav` | Overwritten by Task 7 after audio audition. |

---

## Why the Conductor Also Changes

The composer side alone is not enough. The conductor owns pitch-flow at playback time:

```cpp
// engine/include/mforce/music/conductor.h, current perform_phrase loop body
for (int i = 0; i < fig.note_count(); ++i) {
  const auto& u = fig.units[i];
  if (i > 0) {
    currentNN = step_note(currentNN, u.step, scale);
  }
  ...
```

The `if (i > 0)` is the old rule: "the first unit of a figure doesn't step — it plays at wherever the cursor already is." In the new rule, every unit applies its step. Combined with connector removal, the cursor flows through units continuously: figure N ends with the cursor at some pitch; figure N+1's first unit adds its step to that pitch. Figures with first-unit-step=0 (the common case for composer-generated figures) look identical to the old behavior. Figures with non-zero first-unit-step move the cursor — which is how the new model encodes the inter-figure transition that connectors used to carry.

This is the ONE rule change in `conductor.h`. Delete the `if (i > 0)` guard. Always step.

Then also delete the entire `Apply connector before this figure` block (`conductor.h` around lines 495-514) — that whole switch goes away.

---

## Why the DURN Parser Also Changes

DURN text format has `Xn` tokens between figures that the parser translates into `FigureConnector::step(n)`. With connectors deleted, those Xn tokens need a new home. The semantically correct migration: absorb the `n` into the next figure's first-unit `step` value.

Pre-Phase-1b parse:
```
X-2 <figure A> X3 <figure B>
→ Phrase { figures=[A, B], connectors=[step(-2), step(3)] }
```

Post-Phase-1b parse:
```
X-2 <figure A, first unit step=sA0> X3 <figure B, first unit step=sB0>
→ Phrase { figures=[A with first step = sA0 + (-2), B with first step = sB0 + 3] }
```

The `Xn` BEFORE the first figure of a phrase also gets absorbed (into figure A's first unit step). The `Xn` after the last figure is a no-op (no figure to absorb into) and is dropped with a warning. The existing DURN corpus files ship untouched — they're re-parsed with the new semantics.

No sample DURN files are in the test input for this plan, so the DURN parser change is verified by build-pass only. If a specific DURN regression is wanted later, it's a follow-on.

---

## Task 1: Add the `Literal` figure source

**Files:**
- Modify: `engine/include/mforce/music/templates.h`
- Modify: `engine/include/mforce/music/templates_json.h`
- Modify: `engine/include/mforce/music/composer.h`
- Read: `engine/include/mforce/music/figures.h:1-80` (for `MelodicFigure` and `FigureUnit` layouts)
- Read: `engine/include/mforce/music/basics.h` (for the `Pitch` type and its pitch-name constructor)

Additive task. Adds a new `FigureSource::Literal` enum variant, a `literalNotes` vector on `FigureTemplate`, JSON parsing, and a new switch case in `DefaultFigureStrategy::realize_figure`. After this task, literal figures can be authored in JSON, but no test case in this plan exercises them — the feature is landed and parsed; actual exercise comes when the golden template is updated in Task 7.

- [ ] **Step 1: Add the enum variant**

In `engine/include/mforce/music/templates.h`, find the `enum class FigureSource` (around line 51). Add a `Literal` value:

```cpp
enum class FigureSource {
    Generate,    // composer creates from constraints
    Reference,   // use a seed figure directly
    Transform,   // derive from a seed or previous figure
    Locked,      // fixed content, don't touch
    Literal,     // user-authored note list (pitch + duration per note)
};
```

- [ ] **Step 2: Add the literal notes field on `FigureTemplate`**

Still in `templates.h`, find the `struct FigureTemplate` block. Add, after the `lockedFigure` optional:

```cpp
    // --- For Literal ---
    struct LiteralNote {
      Pitch pitch;
      float duration{1.0f};  // in beats
    };
    std::vector<LiteralNote> literalNotes;
```

- [ ] **Step 3: JSON: round-trip the `Literal` enum value**

In `engine/include/mforce/music/templates_json.h`, find the `FigureSource` `to_json` / `from_json` block (grep for `"generate"`). Add the new mapping:

```cpp
// to_json:
case FigureSource::Literal:   j = "literal"; break;

// from_json:
else if (str == "literal")    s = FigureSource::Literal;
```

- [ ] **Step 4: JSON: round-trip the literal notes vector**

Still in `templates_json.h`, find the `to_json(json&, const FigureTemplate&)` function. After the existing `shape*` field emission, add:

```cpp
if (!ft.literalNotes.empty()) {
  json arr = json::array();
  for (auto& ln : ft.literalNotes) {
    json jn;
    jn["pitch"] = ln.pitch;  // or serialize Pitch the same way the code does elsewhere
    jn["duration"] = ln.duration;
    arr.push_back(std::move(jn));
  }
  j["literalNotes"] = std::move(arr);
}
```

Find the `from_json(const json&, FigureTemplate&)` function. After the existing `shapeParam2` read, add:

```cpp
if (j.contains("literalNotes")) {
  ft.literalNotes.clear();
  for (auto& jn : j.at("literalNotes")) {
    FigureTemplate::LiteralNote ln;
    from_json(jn.at("pitch"), ln.pitch);  // use the existing Pitch from_json
    ln.duration = jn.value("duration", 1.0f);
    ft.literalNotes.push_back(std::move(ln));
  }
}
```

**Note on Pitch serialization.** Search `templates_json.h` and `music_json.h` for how `Pitch` is currently serialized by the `startingPitch` field loader (around `templates_json.h:265` for the deserialize side). Reuse that pattern. If `Pitch` does not already have its own `to_json`/`from_json`, use the same inline `{octave, pitch}` pattern the phrase startingPitch code uses, don't invent a new one.

- [ ] **Step 5: Handle the `Literal` case in `DefaultFigureStrategy::realize_figure`**

In `engine/include/mforce/music/composer.h`, find `DefaultFigureStrategy::realize_figure` (it's defined below the `Composer` class, around line 243 in the current file). Add a new case to the `switch (figTmpl.source)`:

```cpp
case FigureSource::Literal: {
  // Convert the user-authored note list into a MelodicFigure. Each
  // LiteralNote becomes one FigureUnit with its duration; the unit's
  // `step` value is computed relative to the previous note in the list
  // (so rendering this figure through the cursor model reproduces the
  // typed pitches, given the correct cursor-at-start).
  //
  // For the FIRST note, `step` is the delta from ctx.cursor (the passage
  // or phrase cursor position) to that note. This means literal figures
  // are cursor-aware: if the cursor is already at the first literal
  // note's pitch, first step is 0; otherwise first step encodes the
  // transition into the literal.
  //
  // Phase 1b note: this implementation depends on the cursor rename
  // landing first (Task 3). If this task lands before Task 3, use
  // ctx.startingPitch here and rename to ctx.cursor in Task 3.
  MelodicFigure fig;
  if (figTmpl.literalNotes.empty()) return fig;

  // Scale-degree math: compute scale-degree for each pitch relative to
  // ctx.scale, then compute deltas between consecutive notes, and store
  // those as FigureUnit.step values.
  auto degree = [&](const Pitch& p) {
    return DefaultPhraseStrategy::degree_in_scale(p, ctx.scale);
  };
  int prevDeg = degree(ctx.cursor);  // or ctx.startingPitch — see note above
  for (auto& ln : figTmpl.literalNotes) {
    int d = degree(ln.pitch);
    FigureUnit u;
    u.step = d - prevDeg;
    u.duration = ln.duration;
    fig.units.push_back(u);
    prevDeg = d;
  }
  return fig;
}
```

**Important caveats for the implementer:**
- `DefaultPhraseStrategy::degree_in_scale` is a private static. To call it from `DefaultFigureStrategy::realize_figure`, either (a) make it public, or (b) copy the four-line body into a free helper in an anonymous namespace in `composer.h`. Choose (a) — the method is self-contained and tiny.
- The literal-to-steps conversion loses information when a literal note's pitch is not on the current scale (`degree_in_scale` rounds to nearest). That's acceptable for v1 — flag it as a Phase 2+ refinement if the golden audition reveals it matters.
- If `ctx.cursor` (or `ctx.startingPitch` pre-Task-3) equals the first literal note's pitch, the first unit's step is 0, which matches the common case.

- [ ] **Step 6: Build and commit**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. `FigureSource::Literal` is now a valid enum value but nothing in the current golden template uses it, so the new code path is not exercised at runtime.

```
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h engine/include/mforce/music/composer.h
git commit -m "feat(composer): add literal figure source for hand-authored note lists"
```

**Do NOT**
- Exercise the literal path yet (no template changes).
- Touch `figures.h`, `structure.h`, `conductor.h`, or `dun_parser.h` in this task.
- Rename `startingPitch` to `cursor` yet — that's Task 3.

---

## Task 2: Add required `startingPitch` to `PassageTemplate`

**Files:**
- Modify: `engine/include/mforce/music/templates.h`
- Modify: `engine/include/mforce/music/templates_json.h`
- Modify: `patches/template_golden_phase1a.json` (add the field so it still loads)

The field is REQUIRED — the loader refuses to parse a `PassageTemplate` that doesn't have one. This task adds the field, adds the JSON round-trip, updates the golden template, and builds. It does NOT yet use the new field in the composer — that's Task 4.

- [ ] **Step 1: Add the field on `PassageTemplate`**

In `templates.h`, find `struct PassageTemplate` (around line 140). Add `startingPitch` as a required field (no default — the loader will initialize it):

```cpp
struct PassageTemplate {
    std::string name;
    Pitch startingPitch;                          // REQUIRED: initial cursor for this passage
    std::vector<PhraseTemplate> phrases;

    // Passage-level directives
    std::string character;           // freeform: "energetic", "lyrical", "transitional"
    std::string fromKey;             // for modulatory passages
    std::string toKey;               // ending key (empty = same)

    // State
    uint32_t seed{0};
    bool locked{false};
};
```

Note: `Pitch` is not default-constructible with a sensible value (it has a pitchDef pointer). If the compiler complains, use `std::optional<Pitch> startingPitch;` and have the loader refuse the template when it's `std::nullopt`. Prefer non-optional if Pitch has a usable default ctor; otherwise optional + validation.

- [ ] **Step 2: JSON serialize**

In `templates_json.h`, find `to_json(json&, const PassageTemplate&)` (grep for `PassageTemplate`). Add:

```cpp
j["startingPitch"] = pt.startingPitch;
```

(Put it before the phrases field — deterministic ordering is nice but not required.)

- [ ] **Step 3: JSON deserialize with validation**

In the same file, find `from_json(const json&, PassageTemplate&)`. Add a required-field check:

```cpp
if (!j.contains("startingPitch")) {
  throw std::runtime_error(
    "PassageTemplate '" + j.value("name", std::string("<unnamed>")) +
    "' is missing required field 'startingPitch'");
}
from_json(j.at("startingPitch"), pt.startingPitch);
```

Use the same deserialization pattern as `PhraseTemplate::startingPitch` (roughly `templates_json.h:265`). Reuse the existing Pitch from_json function.

- [ ] **Step 4: Update the golden template**

Open `patches/template_golden_phase1a.json`. Find the `"Main"` passage (inside `parts[0].passages`). Add a `startingPitch` field at the passage level:

```json
"passages": {
  "Main": {
    "startingPitch": {"octave": 4, "pitch": "G"},
    "phrases": [ ... ]
  }
}
```

Keep all the existing per-phrase `startingPitch` fields in place — they remain as overrides under the new model, which is what the existing template already expects.

- [ ] **Step 5: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build.

- [ ] **Step 6: Smoke-test loading**

Run the existing composer against the golden template. At THIS task, `PassageTemplate::startingPitch` is read by the JSON loader but not yet used by the composer (the composer still hardcodes `reader.set_pitch(5, 0)` at the start of each passage). So the render should be **bit-identical to the Phase 1a golden** because the new field hasn't yet affected behavior.

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/kick_drum.json renders/phase1b_task2_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/phase1b_task2_check_1.wav
```

Expected hash: `a273a55077727dba211d33ffe0a1cce0e712d0eb2aa3aab7e4e0ea2df34b671f` (the Phase 1a golden).

If the hash differs, either the new field is already being consumed (Task 4 leaked into this task) or the JSON serialization accidentally changed the masterSeed or some other field. Diagnose before moving on.

- [ ] **Step 7: Commit**

```
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h patches/template_golden_phase1a.json
git commit -m "feat(composer): require PassageTemplate startingPitch for cursor model"
```

**Do NOT**
- Change the composer logic to use the new field yet.
- Remove the per-phrase `startingPitch` fields in the golden template.
- Delete any other JSON field.

---

## Task 3: Rename `StrategyContext::startingPitch` → `cursor`

**Files:**
- Modify: `engine/include/mforce/music/strategy.h`
- Modify: `engine/include/mforce/music/composer.h`
- Modify: `engine/include/mforce/music/default_strategies.h`

Cosmetic rename, scoped to the `StrategyContext` struct. Find every reference in the three files above and rename. No other file references `ctx.startingPitch`.

- [ ] **Step 1: Rename the struct field**

In `engine/include/mforce/music/strategy.h`, find `StrategyContext` and change:

```cpp
Pitch startingPitch;
```

to:

```cpp
Pitch cursor;   // current PitchReader-equivalent cursor: passage sets it,
                // phrases may override, figures advance it via their step sequence.
```

- [ ] **Step 2: Update all read/write sites**

Grep for `ctx.startingPitch` and `context.startingPitch` in the three listed files. There should be only a handful:

- `DefaultPassageStrategy::realize_passage` (in `composer.h`, out-of-line) writes `phraseCtx.startingPitch = reader.get_pitch();` — change to `phraseCtx.cursor = reader.get_pitch();`.
- `DefaultPhraseStrategy::realize_phrase` reads `phrase.startingPitch = ctx.startingPitch;` in the fallback branch — change to `phrase.startingPitch = ctx.cursor;`. (Note: `Phrase::startingPitch` itself stays; only the context field renames.)
- If the `Literal` case in `DefaultFigureStrategy::realize_figure` from Task 1 used `ctx.startingPitch`, change it to `ctx.cursor`.

Every other `startingPitch` reference is on `PhraseTemplate`, `Phrase`, or `PassageTemplate` — those names DO NOT rename. Only the `StrategyContext` field renames.

- [ ] **Step 3: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. If compilation fails, there's a stray `ctx.startingPitch` reference you missed. Find it and rename.

- [ ] **Step 4: Smoke-test hash**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/kick_drum.json renders/phase1b_task3_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/phase1b_task3_check_1.wav
```

Expected hash: `a273a55077727dba211d33ffe0a1cce0e712d0eb2aa3aab7e4e0ea2df34b671f` — cosmetic rename should not change output.

- [ ] **Step 5: Commit**

```
git add engine/include/mforce/music/strategy.h engine/include/mforce/music/composer.h engine/include/mforce/music/default_strategies.h
git commit -m "refactor(composer): rename StrategyContext::startingPitch to cursor"
```

---

## Task 4: Switch to the cursor model (the semantic change)

**Files:**
- Modify: `engine/include/mforce/music/composer.h` (DefaultPassageStrategy, DefaultPhraseStrategy, DefaultFigureStrategy Literal case if needed)
- Modify: `engine/include/mforce/music/conductor.h` (perform_phrase rewrite)

This is the task where output changes. After this task, the golden hash will NOT match the Phase 1a pin. That's expected.

- [ ] **Step 1: Update `DefaultPassageStrategy::realize_passage`**

In `composer.h` (out-of-line definition, currently around line 284), replace the body. Old behavior: construct a `PitchReader`, reset to (5, 0), pass that as the fallback to each phrase. New behavior: use `PassageTemplate::startingPitch` as the initial cursor, then carry the cursor across phrases.

```cpp
inline Passage DefaultPassageStrategy::realize_passage(
    const PassageTemplate& passTmpl, StrategyContext& ctx) {
  Passage passage;

  // Initial cursor: the passage template's startingPitch (required field,
  // validated by the JSON loader).
  ctx.cursor = passTmpl.startingPitch;

  for (auto& phraseTmpl : passTmpl.phrases) {
    if (phraseTmpl.locked) continue;

    // Clone the context for the phrase level. If the phrase template has
    // an explicit startingPitch, that's an override — it resets the
    // cursor at the start of the phrase. Otherwise the cursor is
    // inherited from wherever the previous phrase left it.
    StrategyContext phraseCtx = ctx;
    if (phraseTmpl.startingPitch) {
      phraseCtx.cursor = *phraseTmpl.startingPitch;
    }
    // else: phraseCtx.cursor already holds the inherited value.

    Phrase phrase = ctx.composer->realize_phrase(phraseTmpl, phraseCtx);
    passage.add_phrase(std::move(phrase));

    // Carry the cursor forward for the next phrase. The Phrase's final
    // cursor position is computed by summing net_step across all figures
    // (no connectors anymore). Use degree_in_scale + a local scale walk
    // to update ctx.cursor.
    //
    // Simplest correct implementation: walk the figures just once,
    // applying their net_step sum as scale-degree movement from the
    // phrase's starting cursor, and set ctx.cursor to the result.
    //
    // Alternative: have DefaultPhraseStrategy::realize_phrase return the
    // ending cursor alongside the Phrase (tuple or out-param). Cleaner,
    // but breaks the Strategy base class signature. Use the walk-sum
    // approach for Phase 1b; revisit in Phase 2 if the ergonomics hurt.
    //
    // To walk: compute total net scale-degree delta as
    //   delta = sum of (sum of u.step over all units in each figure)
    // and advance ctx.cursor by delta scale-degrees via a PitchReader.
    PitchReader reader(ctx.scale);
    reader.set_pitch(phraseCtx.cursor);   // PitchReader::set_pitch(const Pitch&) snaps to nearest degree
    int totalDelta = 0;
    for (auto& fig : phrase.figures) {
      for (auto& u : fig.units) {
        totalDelta += u.step;
      }
    }
    reader.step(totalDelta);
    ctx.cursor = reader.get_pitch();
  }

  return passage;
}
```

**Implementation notes for the author:**
- `PitchReader::set_pitch(const Pitch&)` exists at `pitch_reader.h:36-48` and snaps the reader to the closest scale degree at the given pitch. Use it directly.
- `PitchReader::step(int n)` exists at `pitch_reader.h:51-57` and walks `n` scale degrees up (positive) or down (negative).
- The total-delta computation relies on the fact that figure steps are scale-degree relative. That's consistent with how the rest of the composer treats them.
- If walking cursor through `PitchReader` is too painful, the alternative is: on `Phrase` add an `endingPitch` field that the composer populates at the end of `DefaultPhraseStrategy::realize_phrase`, and read it here. That's a one-field addition on `Phrase` and eliminates the walk-sum duplication.

- [ ] **Step 2: Update `DefaultPhraseStrategy::realize_phrase`**

In `composer.h`, out-of-line body (currently around line 319). Changes:

1. The per-phrase `startingPitch` fallback still uses `ctx.cursor` (renamed in Task 3). Good.
2. **Delete the connector-insertion branch.** The `else` clause that currently does `phrase.add_figure(std::move(fig), conn)` goes away. All figures use the no-connector `add_figure(fig)` overload.
3. **Delete the `FigureConnector conn = FigureConnector::step(-1)` default**. Gone entirely.
4. The loop becomes:

```cpp
for (int i = 0; i < numFigs; ++i) {
  FigureTemplate figTmpl = phraseTmpl.figures[i];
  if (phraseTmpl.function != MelodicFunction::Free
      && figTmpl.source == FigureSource::Generate
      && figTmpl.shape == FigureShape::Free) {
    figTmpl.shape = DefaultFigureStrategy::choose_shape(
        phraseTmpl.function, i, numFigs, ctx.rng->rng());
  }

  StrategyContext figCtx = ctx;
  MelodicFigure fig = ctx.composer->realize_figure(figTmpl, figCtx);
  phrase.add_figure(std::move(fig));
}
```

(The `if (i == 0) / else` split is gone because there's no per-figure connector anymore. Every figure gets added without a connector.)

- [ ] **Step 3: Update `DefaultPhraseStrategy::apply_cadence`**

In `composer.h` (or still in `default_strategies.h` depending on where it ended up after Phase 1a's header-cycle fix), find `apply_cadence`. The function currently contains:

```cpp
for (int f = 0; f < phrase.figure_count(); ++f) {
  netSteps += phrase.figures[f].net_step();
  if (f > 0 && f - 1 < int(phrase.connectors.size())) {
    const auto& conn = phrase.connectors[f - 1];
    if (conn.type == ConnectorType::Step) netSteps += conn.stepValue;
  }
}
```

Delete the connector-reading block. The loop becomes:

```cpp
for (int f = 0; f < phrase.figure_count(); ++f) {
  netSteps += phrase.figures[f].net_step();
}
```

The rest of the function is unchanged.

**Note:** `MelodicFigure::net_step()` currently returns `sum of u.step for i > 0` (excluding the first unit, because under the old model the first unit's step was ignored). Under the new cursor model, the first unit's step is part of the figure's net movement, so `net_step()` must include it.

**Check `net_step()` in `figures.h`** — find the method, read its body. If it starts at index 1, update it to start at 0. This is a tiny but LOAD-BEARING change: if `net_step()` doesn't include the first unit, `apply_cadence` computes the wrong net movement for the whole phrase. Include `figures.h` in this task's file list if you make this change:

```cpp
// In figures.h, MelodicFigure::net_step():
int net_step() const {
  int sum = 0;
  for (auto& u : units) sum += u.step;   // changed: iterate all units, not starting from 1
  return sum;
}
```

If the existing `net_step()` already sums from index 0, do nothing and don't touch `figures.h`.

- [ ] **Step 4: Rewrite `Conductor::perform_phrase`**

In `engine/include/mforce/music/conductor.h`, find `perform_phrase`. Two changes:

**A. Delete the connector switch.** Remove the entire block currently at `conductor.h:495-514`:

```cpp
// DELETE THIS BLOCK:
if (f > 0 && f - 1 < int(phrase.connectors.size())) {
  const auto& conn = phrase.connectors[f - 1];
  switch (conn.type) {
    case ConnectorType::Step: ...
    case ConnectorType::Pitch: ...
    case ConnectorType::EndPitch: ...
    case ConnectorType::Elide: ...
  }
}
```

**B. Apply every unit's step, including the first.** Remove the `if (i > 0)` guard in the inner unit loop. Change:

```cpp
for (int i = 0; i < fig.note_count(); ++i) {
  const auto& u = fig.units[i];
  if (i > 0) {
    currentNN = step_note(currentNN, u.step, scale);
  }
  ...
```

to:

```cpp
for (int i = 0; i < fig.note_count(); ++i) {
  const auto& u = fig.units[i];
  currentNN = step_note(currentNN, u.step, scale);
  ...
```

That's the entire `perform_phrase` change. Leave everything else (dynamics, rest handling, articulation, etc.) untouched.

- [ ] **Step 5: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. If the build fails, diagnose file-by-file. The most likely failure modes:

- `FigureConnector` or `ConnectorType` still referenced somewhere in `composer.h` / `default_strategies.h` (a missed delete in apply_cadence or realize_phrase).
- `phrase.connectors` still referenced — grep the file for it and delete every read/write.
- `PitchReader::set_pitch_exact` doesn't exist — use the correct method name.
- `MelodicFigure::net_step()` signature drift — if you renamed or changed it, update callers.

**Do NOT work around a build failure by reinstating connector handling. The connectors are gone by design.**

- [ ] **Step 6: Render and confirm output differs from Phase 1a golden**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/kick_drum.json renders/phase1b_task4_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/phase1b_task4_check_1.wav
```

Expected: a hash that is **NOT** `a273a55...`. If it matches, the connector-removal didn't take effect — something still short-circuits the change. Diagnose before committing.

Also: **audition the WAV**. Play it. It should still sound like recognizable music — same tempo, same key, similar overall shape — but with phrase transitions that differ from the Phase 1a render because the `step(-1)` inter-figure default is gone.

If the render is silent, unlistenable, or obviously broken, there's a real bug. Do NOT commit a broken golden. Diagnose:
- Are all the figure first-unit steps reasonable? (Run the CLI and inspect `renders/phase1b_task4_check_1.json` — look for first-unit `step` values. If they're all still 0, the composer is producing the same figure shapes; the difference should only be in inter-figure transitions.)
- Is the passage startingPitch correct? (Should be G4 per the golden template update in Task 2.)
- Did `perform_phrase` lose the articulation handling or the rest handling? (Re-read your diff against the original.)

- [ ] **Step 7: Commit**

```
git add engine/include/mforce/music/composer.h engine/include/mforce/music/conductor.h
git add engine/include/mforce/music/default_strategies.h    # if apply_cadence moved there
git add engine/include/mforce/music/figures.h               # only if net_step changed
git commit -m "feat(composer): switch to cursor model, delete connector logic"
```

**Do NOT**
- Delete the `FigureConnector` type itself in this task. It still exists, unused, after Task 4. Task 6 handles the actual type deletion.
- Touch `dun_parser.h` — that's Task 5.

---

## Task 5: DURN parser absorbs connector steps into first-unit steps

**Files:**
- Modify: `engine/include/mforce/music/dun_parser.h`

The DURN parser still produces `FigureConnector` objects via `phrase.add_figure(fig, FigureConnector::step(n))`. With Phrase's connector overload removed in Task 4 (actually it wasn't removed in Task 4 — that's a Task 6 thing), the parser still builds. But semantically, any `Xn` in a DURN file now gets silently ignored. That's wrong.

The fix: when the parser encounters `Xn` between figure A and figure B, ADD `n` to figure B's first unit step before adding B to the phrase. The `Xn` before the first figure of a phrase gets added to figure 1's first unit step.

- [ ] **Step 1: Read the current parser**

Read `engine/include/mforce/music/dun_parser.h` around lines 240-350 (the phrase-parse loop). Find the `connStep` variable and the `phrase.add_figure(std::move(fig), FigureConnector::step(connStep));` call. That's the target.

- [ ] **Step 2: Rewrite the Xn handling**

Replace the connector-emitting call with a first-unit-step absorption. The accumulator variable (call it `pendingConnStep`) holds the most recent `Xn` value seen since the last figure was emitted. When a new figure is about to be added to the phrase, add `pendingConnStep` to `fig.units[0].step` and reset `pendingConnStep` to 0:

```cpp
// Conceptually, at the point where a figure is ready to push:
if (!fig.units.empty()) {
  fig.units[0].step += pendingConnStep;
}
phrase.add_figure(std::move(fig));
pendingConnStep = 0;

// And when an Xn token is parsed:
pendingConnStep = <parsed n>;
```

This absorbs ALL `Xn` tokens into the next figure, including the one before the first figure of a phrase. An `Xn` after the last figure of a phrase has no next figure to absorb into — emit a debug log line saying "dropping trailing Xn in phrase <name>" and move on.

- [ ] **Step 3: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build.

- [ ] **Step 4: Smoke-test the parser**

Run the DURN converter on a known-good DURN input (if one exists) and confirm it produces JSON that still loads through the normal composer path. If no DURN test input is handy, skip this step — the parser is verified by build-pass and the fact that it no longer calls any connector-producing method.

- [ ] **Step 5: Commit**

```
git add engine/include/mforce/music/dun_parser.h
git commit -m "refactor(dun): absorb connector steps into figure first-unit steps"
```

---

## Task 6: Delete the `FigureConnector` type entirely

**Files:**
- Modify: `engine/include/mforce/music/figures.h` (delete `FigureConnector` + `ConnectorType`)
- Modify: `engine/include/mforce/music/structure.h` (delete `Phrase::connectors` + the `add_figure(fig, conn)` overload)
- Modify: `engine/include/mforce/music/templates.h` (delete `PhraseTemplate::connectors`)
- Modify: `engine/include/mforce/music/music_json.h` (delete `ConnectorType` + `FigureConnector` serialization, delete `Phrase::connectors` serialization)
- Modify: `engine/include/mforce/music/templates_json.h` (delete `PhraseTemplate::connectors` serialization)

By now, nothing READS from `FigureConnector` (Task 4 removed the composer and conductor uses; Task 5 removed the DURN parser use). Everything that references the type does so only because the type still EXISTS and is still a field on some struct. This task nukes it all.

- [ ] **Step 1: Grep for callers**

```
grep -rn "FigureConnector\|ConnectorType\|\.connectors\b" engine tools
```

Expected matches, all in the files listed above (and in commented-out or doc comments, which you leave alone unless they mention the actual type). If there's any other hit, surface it and resolve before proceeding.

- [ ] **Step 2: Delete from `figures.h`**

Find `enum class ConnectorType` and `struct FigureConnector` (around `figures.h:370-379`). Delete the whole block, including the `FigureConnector::step` and `FigureConnector::elide` helpers.

- [ ] **Step 3: Delete from `structure.h`**

Find `struct Phrase`. Delete the line:

```cpp
std::vector<FigureConnector> connectors;  // between adjacent figures
```

And delete the overload:

```cpp
void add_figure(MelodicFigure fig, FigureConnector conn) {
  connectors.push_back(std::move(conn));
  figures.push_back(std::move(fig));
}
```

Keep the no-connector `add_figure(MelodicFigure fig)` overload and `figure_count()`.

- [ ] **Step 4: Delete from `templates.h`**

Find `struct PhraseTemplate`. Delete the line:

```cpp
std::vector<FigureConnector> connectors;   // connectors[i] connects figure[i] to figure[i+1]
```

Update the comment above the struct (it currently says "sequence of FigureTemplates with connectors" — change to "sequence of FigureTemplates" or similar).

- [ ] **Step 5: Delete from `music_json.h`**

Find the `ConnectorType` `to_json`/`from_json` (around lines 74-83) and the `FigureConnector` `to_json`/`from_json` (around lines 286-295). Delete both blocks.

Find the `Phrase` serialization and delete the `connectors` round-trip (around `music_json.h:387` and `397-401`).

- [ ] **Step 6: Delete from `templates_json.h`**

Find the `PhraseTemplate` serialization and delete any `connectors` field round-trip. Grep for "connectors" in the file to be sure.

- [ ] **Step 7: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build. If there's any leftover `FigureConnector` reference anywhere (including `tools/mforce_cli/main.cpp`, which you didn't touch but should grep), fix it.

- [ ] **Step 8: Smoke-test**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/kick_drum.json renders/phase1b_task6_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/phase1b_task6_check_1.wav
```

Expected: the hash from Task 4 Step 6. Deleting the dead type should not change output — the only thing happening is removing unused symbols. If the hash differs from Task 4's output, something in the deletion was more than just dead-code removal. Diagnose before committing.

- [ ] **Step 9: Commit**

```
git add engine/include/mforce/music/figures.h engine/include/mforce/music/structure.h engine/include/mforce/music/templates.h engine/include/mforce/music/music_json.h engine/include/mforce/music/templates_json.h
git commit -m "refactor(composer): delete FigureConnector and all connector plumbing"
```

---

## Task 7: Audition the new golden and re-pin the hash

**Files:**
- Modify: `renders/template_golden_phase1a.wav` (rewrite)
- Modify: `renders/template_golden_phase1a.sha256` (rewrite)

The composer behavior is now fully Phase-1b. Task 7 captures the new golden.

- [ ] **Step 1: Re-render**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/kick_drum.json renders/template_golden_phase1a 1 --template patches/template_golden_phase1a.json
```

Expected output: `renders/template_golden_phase1a_1.wav` (the CLI appends `_1` to the prefix).

- [ ] **Step 2: Copy to the committed golden filename**

```
cp renders/template_golden_phase1a_1.wav renders/template_golden_phase1a.wav
```

- [ ] **Step 3: Human audio audition**

**STOP. Do not continue until the WAV is auditioned.**

Play `renders/template_golden_phase1a.wav`. Listen for:

- Clearly recognizable melody, not noise or silence.
- Key is G major. The passage starts around G4 (per the golden template's passage startingPitch).
- Four phrases: A1, B, A2, A3 — each roughly 12 beats of melody plus a cadential note.
- No crashes, no clipping, no abrupt pitch jumps that sound like bugs rather than music (a little unexpected pitch movement between figures is normal — that's the point of the connector removal).

If the audition fails (sounds broken): DO NOT commit. Diagnose by inspecting `renders/template_golden_phase1a_1.json` for the composed pitch sequence. Compare against the Phase 1a render you committed at `renders/template_golden_phase1a.wav` (before this task rewrote it). If you need to regenerate the Phase 1a reference, check out `95fa026` in a separate worktree.

If the audition passes: continue.

- [ ] **Step 4: Recompute the hash**

```
sha256sum renders/template_golden_phase1a.wav
```

Capture the new hash. Rewrite the `.sha256` file:

```
sha256sum renders/template_golden_phase1a.wav > renders/template_golden_phase1a.sha256
```

- [ ] **Step 5: Commit the new golden**

```
git add -f renders/template_golden_phase1a.wav
git add renders/template_golden_phase1a.sha256
git commit -m "test: re-pin golden for Phase 1b cursor model + connector removal

Phase 1a pinned hash a273a55... under the connector model. Phase 1b
deletes connectors and introduces the running-cursor semantics; the new
golden reflects the intended output of that semantic change, auditioned
by the author before commit."
```

- [ ] **Step 6: Final orphan sweep**

```
grep -rn "FigureConnector\|ConnectorType\|Phrase::connectors\|PhraseTemplate::connectors\|\.connectors\b" engine tools docs/superpowers/plans/2026-04-10-composer-strategy-phase-1b-semantic-changes.md
```

Expected matches: only in this plan document (Phase 1b plan explicitly mentions the deleted types in its own text) and possibly in `docs/`. Nothing in `engine/` or `tools/`. If there is, surface and fix it.

---

## Phase 1b exit criteria

All of the following must be true before declaring Phase 1b done:

1. `cmake --build build --target mforce_cli --config Release` succeeds from a clean tree.
2. `renders/template_golden_phase1a.sha256` contains the new hash and matches `sha256sum renders/template_golden_phase1a.wav`.
3. `grep -rn "FigureConnector\|ConnectorType" engine tools` shows no hits.
4. `grep -rn "\.connectors\b" engine tools` shows no hits.
5. The CLI invocation `build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/kick_drum.json renders/golden_check 1 --template patches/template_golden_phase1a.json` reproduces the committed hash byte-for-byte.
6. Audio audition passed: the render is recognizable music, not broken.
7. `PassageTemplate::startingPitch` is required and loaded from every template.
8. `FigureSource::Literal` is accepted by the JSON loader and the composer has a code path for it.
9. `StrategyContext` field is named `cursor`, not `startingPitch`.
10. The Phase 1a golden hash (`a273a55...`) is NOT expected to match post-Phase-1b. Do not attempt to chase it.

---

## What is explicitly NOT in this plan

- No new phrase/passage/figure strategies beyond the Defaults (Phase 3/4).
- No `Seed → Motif` rename (Phase 5).
- No template `strategy` field support in dispatch (Phase 3+).
- No unit test framework.
- No refactoring of `PitchReader`, `Scale`, `Pitch`, or other primitives.
- No changes to `FigureBuilder`, `StepGenerator`, or any music primitive construction.
- No audio-quality analysis or automated audition — the audit is human.
- No migration script for existing DURN/JSON corpus files (DURN is handled in Task 5; the existing JSON templates already update via Task 2's golden edit; `template_shaped_test.json`, `template_mary.json`, `template_binary.json`, and the `template_ode_to_joy*.json` files are NOT updated because they're not exercised by Phase 1b's regression check — they'll need passage startingPitch added before they can load again, which is a drive-by we defer to whoever next uses them).
- No literal-figure test in the golden template. Literal source is LANDED but not EXERCISED by this plan. Exercising it is a Phase 2+ task.
- No rewrite of `Conductor::perform_figure` or dynamics handling beyond the two targeted edits in Task 4.
