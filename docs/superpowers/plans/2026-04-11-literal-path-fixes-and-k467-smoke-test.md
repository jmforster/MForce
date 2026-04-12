# Literal Path Fixes + K467 Smoke Test — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix four latent issues in the Phase 1b Literal figure path that block fully-prescriptive template authoring, then author a literal-figures template reproducing the opening 4 bars of Mozart K467 movement I, render it, confirm it's non-silent and non-crashing. Matt auditions the WAV on return and decides whether the "literal path works" premise is validated.

**Architecture:** Surgical fixes to `DefaultFigureStrategy::realize_figure` (the Literal case), `DefaultPhraseStrategy::realize_phrase` (intra-phrase cursor tracking + cadence skip for literals), and `LiteralNote` (rest support). Single new template file. No new strategy classes, no new config structs, no framework-level changes.

**Tech Stack:** C++17, header-only, CMake. Mechanical hash verification — the golden `0815d32e...` must stay unchanged after each fix (none of the fixes touch code paths the golden exercises).

---

## Scope Guardrails

- **Golden hash preservation is mandatory at every task.** `renders/template_golden_phase1a.wav` and its `.sha256` must continue to hash to `0815d32ebc8ae34c79a4dc1b8c069fe88538fdc373b0e2cb3a934c1164a0f1ff` after each commit. All four fixes touch code paths that the golden template (which uses Generate-source figures, no Literal figures, no connectors, no non-Default strategies) does not exercise. A drift signals the fix leaked into the default path.
- **Literal path bug fixes only.** No changes to Generate, Reference, Transform, Locked, or shape strategies.
- **No new strategy classes.** Period/Sentence configs still hold `MelodicFigure` (delta form). The Issue 1 "convert configs to FigureTemplate" work is out of scope for this plan and will come after the audit.
- **No Outline strategies.** Deferred pending the K467 audit outcome.
- **No audition step in execution.** Matt auditions the WAV on return. This plan verifies non-silent + non-crashing, not musical fidelity.
- **No pinning of the K467 WAV as a golden.** The template is committed; the render is captured as a scratch file that gets cleaned up. Matt re-renders when he gets back.
- **Single branch: `main`.** Commit directly.

---

## File structure

Modified files:

| File | Change |
|---|---|
| `engine/include/mforce/music/templates.h` | Add `bool rest{false};` to `FigureTemplate::LiteralNote`. |
| `engine/include/mforce/music/templates_json.h` | JSON round-trip for `LiteralNote::rest`. |
| `engine/include/mforce/music/composer.h` | In out-of-line `DefaultFigureStrategy::realize_figure`, replace the Literal case's modular `degree_in_scale` math with octave-aware absolute scale-degree positions; handle `rest` flag. |
| `engine/include/mforce/music/default_strategies.h` OR `engine/include/mforce/music/composer.h` (wherever `DefaultPhraseStrategy::realize_phrase` lives) | Maintain an intra-phrase running cursor and pass it into each figure's `figCtx.cursor`. Also skip `apply_cadence` when the last figure template's source is Literal or Locked. |

New files:

| File | Purpose |
|---|---|
| `patches/test_k467_opening.json` | 4-bar literal template reproducing K467/i opening, C major, 100 bpm, 2 phrases × 2 figures, all `source: "literal"`. |

Files NOT touched:
- `figures.h` (FigureUnit already has `rest` and `step` fields)
- `structure.h`, `strategy.h`, `strategy_registry.h`, `classical_composer.h`, `music_json.h`, `conductor.h`, `dun_parser.h`, `shape_strategies.h`, `phrase_strategies.h`, `outline_strategies.h` (does not yet exist), `basics.h`, `pitch_reader.h`
- Any other template JSON
- Any file under `tools/`

---

## Task 1: Octave-aware scale-degree math in the Literal path

**Files:**
- Modify: `engine/include/mforce/music/composer.h` (out-of-line `DefaultFigureStrategy::realize_figure`, Literal case)

**Problem**: `DefaultPhraseStrategy::degree_in_scale` returns a modular scale degree in `[0, scale.length())`, so C4 and C5 both return 0, G3 and G4 both return 4. The Literal case currently computes `u.step = d - prevDeg` using these modular values, so an octave-crossing literal like C4→G3 produces `step = 4 - 0 = +4` instead of the intended `-3`. The conductor then walks +4 from C4, landing on G4 (wrong octave).

**Fix**: introduce an `absoluteDeg` helper in the Literal case that multiplies by `ctx.scale.length()` to produce octave-adjusted scale-degree positions.

- [ ] **Step 1: Read the current Literal case**

Read `engine/include/mforce/music/composer.h` out-of-line `DefaultFigureStrategy::realize_figure` (below the `Composer` class definition). Find the `case FigureSource::Literal:` block. Expected current shape:

```cpp
case FigureSource::Literal: {
  MelodicFigure fig;
  if (figTmpl.literalNotes.empty()) return fig;

  auto degree = [&](const Pitch& p) {
    return DefaultPhraseStrategy::degree_in_scale(p, ctx.scale);
  };
  int prevDeg = degree(ctx.cursor);
  for (auto& ln : figTmpl.literalNotes) {
    if (!ln.pitch) continue;
    int d = degree(*ln.pitch);
    FigureUnit u;
    u.step = d - prevDeg;
    u.duration = ln.duration;
    fig.units.push_back(u);
    prevDeg = d;
  }
  return fig;
}
```

- [ ] **Step 2: Replace with octave-aware version**

Replace the `degree` lambda and its use with `absoluteDeg`:

```cpp
case FigureSource::Literal: {
  MelodicFigure fig;
  if (figTmpl.literalNotes.empty()) return fig;

  // Octave-adjusted scale-degree position: for C major with scale.length()=7,
  // C4 = 4*7 + 0 = 28, G3 = 3*7 + 4 = 25, so going C4→G3 yields step = -3
  // which the conductor walks as three scale-steps down (C→B→A→G), landing
  // at G3 in the correct octave. Without the octave multiplier, the modular
  // degree-only math would yield step = +4 and land at G4.
  auto absoluteDeg = [&](const Pitch& p) {
    int d = DefaultPhraseStrategy::degree_in_scale(p, ctx.scale);
    return p.octave * ctx.scale.length() + d;
  };

  int prevDeg = absoluteDeg(ctx.cursor);
  for (auto& ln : figTmpl.literalNotes) {
    if (!ln.pitch) continue;
    int d = absoluteDeg(*ln.pitch);
    FigureUnit u;
    u.step = d - prevDeg;
    u.duration = ln.duration;
    fig.units.push_back(u);
    prevDeg = d;
  }
  return fig;
}
```

Confirm `Pitch` has a `.octave` field (int). Grep `basics.h` for `struct Pitch` — it should show `int octave;` or similar. If the field name differs (e.g. `octaveNumber`), use the actual name.

Confirm `Scale::length()` exists and returns an int. Already used in `degree_in_scale`'s body, so it's known to exist.

- [ ] **Step 3: Build**

```
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Release
```

Expected: clean build.

- [ ] **Step 4: Verify golden hash is unchanged**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/t1_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/t1_check_1.wav
```

Expected hash: `0815d32ebc8ae34c79a4dc1b8c069fe88538fdc373b0e2cb3a934c1164a0f1ff`. The golden template uses no Literal figures, so the Literal case is unreachable and the change is inert for the golden.

- [ ] **Step 5: Commit**

```
git add engine/include/mforce/music/composer.h
git commit -m "fix(composer): octave-aware scale-degree math in Literal path

degree_in_scale returns modular degrees in [0, scale.length()), so
literal figures crossing octave boundaries (e.g. C4→G3) computed
wrong step deltas (+4 instead of -3) and rendered at the wrong
octave. Fix multiplies octave by scale.length() to produce an
absolute scale-degree position, so step = d - prevDeg captures the
signed distance including octave. Only affects FigureSource::Literal
path, which no existing template exercises — golden hash unchanged."
```

Clean up:
```
rm renders/t1_check_1.wav renders/t1_check_1.json 2>/dev/null
```

---

## Task 2: `LiteralNote` supports rests

**Files:**
- Modify: `engine/include/mforce/music/templates.h` (add `rest` field to `LiteralNote`)
- Modify: `engine/include/mforce/music/templates_json.h` (JSON round-trip)
- Modify: `engine/include/mforce/music/composer.h` (Literal case handles `rest`)

**Problem**: Phase 1b's `LiteralNote` has `{std::optional<Pitch> pitch; float duration}`, no way to encode a rest. K467 bar 2 has a dotted-eighth rest and bar 4 has a quarter rest — essential to the theme's character.

**Fix**: add `bool rest{false}` to `LiteralNote`. When `rest == true`, the Literal case emits a `FigureUnit` with `rest = true, step = 0, duration = ln.duration`, and does not update `prevDeg` (rests don't advance the cursor).

- [ ] **Step 1: Add `rest` field to `LiteralNote`**

In `engine/include/mforce/music/templates.h`, find `FigureTemplate::LiteralNote` (inside `struct FigureTemplate`). Add the `rest` field:

```cpp
struct LiteralNote {
  std::optional<Pitch> pitch;
  float duration{1.0f};  // in beats
  bool rest{false};      // true = silence for `duration` beats; pitch ignored
};
```

- [ ] **Step 2: JSON round-trip for `rest`**

In `engine/include/mforce/music/templates_json.h`, find the `to_json` / `from_json` for `LiteralNote` (or the inline literalNotes emission inside `FigureTemplate`'s serialization). Add the `rest` field:

```cpp
// to_json — emit rest only when true
if (ln.rest) jn["rest"] = true;

// from_json
ln.rest = jn.value("rest", false);
```

If the existing serialization emits `pitch` unconditionally, gate it on `!ln.rest` so that rest-notes don't require a pitch in JSON:

```cpp
// to_json
if (!ln.rest) jn["pitch"] = *ln.pitch;
jn["duration"] = ln.duration;

// from_json
if (j.contains("pitch")) {
  Pitch p;
  from_json(jn.at("pitch"), p);
  ln.pitch = p;
}
ln.duration = jn.value("duration", 1.0f);
ln.rest = jn.value("rest", false);
```

- [ ] **Step 3: Update the Literal case in `composer.h`**

In the out-of-line `DefaultFigureStrategy::realize_figure`, update the Literal case to emit a rest unit when `ln.rest == true`:

```cpp
for (auto& ln : figTmpl.literalNotes) {
  FigureUnit u;
  u.duration = ln.duration;
  if (ln.rest) {
    u.rest = true;
    u.step = 0;
    // prevDeg unchanged — rests don't advance the cursor
  } else {
    if (!ln.pitch) continue;  // defensive; malformed note
    int d = absoluteDeg(*ln.pitch);
    u.step = d - prevDeg;
    prevDeg = d;
  }
  fig.units.push_back(u);
}
```

This replaces the previous loop body (which only handled pitched notes).

- [ ] **Step 4: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build.

- [ ] **Step 5: Verify golden hash is unchanged**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/t2_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/t2_check_1.wav
```

Expected: `0815d32e...`. The golden has no rests in its figures; the change is inert.

- [ ] **Step 6: Commit**

```
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h engine/include/mforce/music/composer.h
git commit -m "feat(composer): add rest field to LiteralNote

LiteralNote previously supported only pitched notes. Mozart and real
classical themes have rests (K467/i bar 2 has a dotted-eighth rest,
bar 4 has a quarter rest). Add bool rest{false}; when true, the
Literal case emits a FigureUnit with rest=true, step=0, and does not
update prevDeg. Golden hash unchanged."
```

Clean up scratch files.

---

## Task 3: Intra-phrase cursor tracking in `DefaultPhraseStrategy::realize_phrase`

**Files:**
- Modify: `engine/include/mforce/music/composer.h` OR `engine/include/mforce/music/default_strategies.h` (wherever `DefaultPhraseStrategy::realize_phrase`'s out-of-line definition lives after Phase 1b)

**Problem**: `DefaultPhraseStrategy::realize_phrase` currently passes the same `ctx` to every figure's `realize_figure`. `ctx.cursor` stays at the phrase's starting cursor regardless of how many figures have been realized. For multi-figure phrases with Literal figures, the second-and-later figures compute their step deltas from a stale cursor, and the conductor plays them at wrong octaves.

**Fix**: maintain a running cursor as a local variable. After each figure is realized, advance a local `PitchReader` through the figure's `net_step()` to get the new cursor pitch. Update `figCtx.cursor` to this new pitch before realizing the next figure.

- [ ] **Step 1: Locate `DefaultPhraseStrategy::realize_phrase`**

Grep for `DefaultPhraseStrategy::realize_phrase` in `engine/include/mforce/music/`. The out-of-line definition is likely in `composer.h` (from Phase 1b Task 7's header-cycle fix). Read the current body to see the figure loop.

Expected shape (approximate, from the Phase 1b spec):

```cpp
inline Phrase DefaultPhraseStrategy::realize_phrase(
    const PhraseTemplate& phraseTmpl, StrategyContext& ctx) {
  Phrase phrase;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ctx.cursor;
  }

  const int numFigs = int(phraseTmpl.figures.size());
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

  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    apply_cadence(phrase, phraseTmpl, ctx.scale);
  }

  return phrase;
}
```

- [ ] **Step 2: Add running-cursor tracking**

Modify the loop to maintain a running cursor and pass it into each figure's context:

```cpp
inline Phrase DefaultPhraseStrategy::realize_phrase(
    const PhraseTemplate& phraseTmpl, StrategyContext& ctx) {
  Phrase phrase;

  if (phraseTmpl.startingPitch) {
    phrase.startingPitch = *phraseTmpl.startingPitch;
  } else {
    phrase.startingPitch = ctx.cursor;
  }

  // Running cursor walks through the phrase as figures play, so each
  // figure sees the cursor position at its start rather than the phrase's
  // starting cursor. Required for Literal and Outline figures whose
  // step math depends on where the previous figure left the cursor.
  PitchReader runningReader(ctx.scale);
  runningReader.set_pitch(phrase.startingPitch);

  const int numFigs = int(phraseTmpl.figures.size());
  for (int i = 0; i < numFigs; ++i) {
    FigureTemplate figTmpl = phraseTmpl.figures[i];
    if (phraseTmpl.function != MelodicFunction::Free
        && figTmpl.source == FigureSource::Generate
        && figTmpl.shape == FigureShape::Free) {
      figTmpl.shape = DefaultFigureStrategy::choose_shape(
          phraseTmpl.function, i, numFigs, ctx.rng->rng());
    }

    StrategyContext figCtx = ctx;
    figCtx.cursor = runningReader.get_pitch();  // cursor seen by this figure
    MelodicFigure fig = ctx.composer->realize_figure(figTmpl, figCtx);

    // Advance running cursor by the figure's net scale-degree movement
    // before dispatching the next figure. rest-units contribute step=0
    // so they don't advance the cursor (which is correct).
    runningReader.step(fig.net_step());

    phrase.add_figure(std::move(fig));
  }

  if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
      && !phrase.figures.empty()) {
    apply_cadence(phrase, phraseTmpl, ctx.scale);
  }

  return phrase;
}
```

Include `#include "mforce/music/pitch_reader.h"` at the top of `composer.h` if not already present (it likely already is — `DefaultPassageStrategy::realize_passage` uses `PitchReader`).

- [ ] **Step 3: Build**

```
cmake --build build --target mforce_cli --config Release
```

Expected: clean build.

- [ ] **Step 4: Verify golden hash is unchanged**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/t3_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/t3_check_1.wav
```

Expected: `0815d32e...`. The golden's Generate-source figures don't consult `figCtx.cursor` — only Literal and Outline figures do, and neither appears in the golden. Any drift means the cursor update accidentally leaked into the Generate path (e.g. `figCtx.cursor` assignment creating a side effect in `ctx`, which it shouldn't since `figCtx` is a copy).

- [ ] **Step 5: Commit**

```
git add engine/include/mforce/music/composer.h
git commit -m "fix(composer): intra-phrase cursor tracking in DefaultPhraseStrategy

DefaultPhraseStrategy::realize_phrase passed the same ctx to every
figure, so the cursor seen by each figure stayed at the phrase start
regardless of how many figures had already been realized. For multi-
figure phrases with Literal (or future Outline) figures whose step
math depends on cursor position, subsequent figures computed wrong
step deltas.

Fix: maintain a local PitchReader that walks through the phrase as
figures are realized. Each figure sees figCtx.cursor set to the
reader's current pitch, and after the figure is realized the reader
steps by net_step() to prepare for the next figure.

Generate-source figures don't consult ctx.cursor so the golden hash
is unchanged."
```

---

## Task 4: `apply_cadence` skips Literal and Locked figures

**Files:**
- Modify: `engine/include/mforce/music/composer.h` OR `engine/include/mforce/music/default_strategies.h` (same location as Task 3)

**Problem**: `apply_cadence` re-pitches the last unit of the last figure to land on `cadenceTarget`. For Literal figures, that means clobbering a note the user explicitly typed. When Matt's K467 phrase 1 lands on B3 as a half cadence, he doesn't want `apply_cadence` moving that B3 to scale degree 4 (whatever that would be).

**Fix**: in `DefaultPhraseStrategy::realize_phrase`, check the last figure's source before calling `apply_cadence`. Skip if `Literal` or `Locked`.

- [ ] **Step 1: Add the source-check before `apply_cadence`**

Modify the tail of `realize_phrase` (same location as Task 3's change):

```cpp
// Cadence adjustment — skip for Literal and Locked source on the last
// figure. The user's exact notes are intentional; don't clobber them.
// For Reference and Transform sources, the figure is derived from a
// motif; cadence adjustment of the LAST unit is acceptable because
// the motif's intent doesn't dictate the closing pitch.
if (phraseTmpl.cadenceType > 0 && phraseTmpl.cadenceTarget >= 0
    && !phrase.figures.empty() && !phraseTmpl.figures.empty()) {
  const FigureSource lastSource = phraseTmpl.figures.back().source;
  if (lastSource != FigureSource::Literal && lastSource != FigureSource::Locked) {
    apply_cadence(phrase, phraseTmpl, ctx.scale);
  }
}
```

- [ ] **Step 2: Build**

```
cmake --build build --target mforce_cli --config Release
```

- [ ] **Step 3: Verify golden hash is unchanged**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/t4_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/t4_check_1.wav
```

Expected: `0815d32e...`. The golden template's figures are all `FigureSource::Generate` / `Reference` / `Transform`, none Literal or Locked, so the new check never suppresses `apply_cadence` on the golden. Behavior is unchanged.

- [ ] **Step 4: Commit**

```
git add engine/include/mforce/music/composer.h
git commit -m "fix(composer): skip apply_cadence for Literal and Locked phrase endings

When a phrase's last figure is Literal or Locked, the user has typed
the exact notes they want. apply_cadence re-pitches the last unit to
land on cadenceTarget, clobbering the authored pitch. Fix checks the
last figure template's source and skips cadence adjustment for
Literal and Locked. Reference and Transform continue to allow
cadence adjustment (motif intent doesn't dictate the closing pitch).
Golden hash unchanged."
```

---

## Task 5: Author `patches/test_k467_opening.json`

**Files:**
- Create: `patches/test_k467_opening.json`

Author a literal-figures template reproducing Mozart K467/i opening 4 bars per Matt's transcription.

**Musical content** (C major, 100 bpm, 16 beats total):

```
Bar 1 (fig1):  C4 q | G3 q | C4 q | E4 q
Bar 2 (fig2):  F4 q. | E4 s3 | D4 s3 | C4 s3 | B3 q | rest e. | G3 s
Bar 3 (fig3):  B3 q | D4 q | F4 q | D4 q
Bar 4 (fig4):  G4 q. | A4 s3 | G4 s3 | F4 s3 | E4 q | rest q
```

**Duration key**:
- `q` (quarter) = 1.0 beat
- `q.` (dotted quarter) = 1.5 beats
- `e.` (dotted eighth) = 0.75 beats
- `s` (sixteenth) = 0.25 beats
- `s3` (sixteenth triplet — three notes in the time of two sixteenths = 0.5 beats total) = 0.5/3 ≈ 0.166666 beats per note

**Phrase structure**:
- Phrase 1 (bars 1–2): fig1 + fig2, half cadence on B3 (^7 over V)
- Phrase 2 (bars 3–4): fig3 + fig4, imperfect authentic cadence on E4 (^3 over I)

Matt's call-out on the cadences: phrase 1 half cadence, phrase 2 IAC. Both phrases have `cadenceType` set and `cadenceTarget` that matches the actual last note, but since all figures are Literal, `apply_cadence` is skipped (Task 4). The cadence fields are informational / documentary, not actively used.

- [ ] **Step 1: Author the JSON**

Create `patches/test_k467_opening.json`:

```json
{
  "keyName": "C",
  "scaleName": "Major",
  "bpm": 100.0,
  "masterSeed": 467,
  "sections": [
    {"name": "Main", "beats": 16}
  ],
  "parts": [
    {
      "name": "melody",
      "role": "melody",
      "passages": {
        "Main": {
          "startingPitch": {"octave": 4, "pitch": "C"},
          "phrases": [
            {
              "name": "Phrase1",
              "startingPitch": {"octave": 4, "pitch": "C"},
              "cadenceType": 1,
              "cadenceTarget": 6,
              "figures": [
                {
                  "source": "literal",
                  "literalNotes": [
                    {"pitch": {"octave": 4, "pitch": "C"}, "duration": 1.0},
                    {"pitch": {"octave": 3, "pitch": "G"}, "duration": 1.0},
                    {"pitch": {"octave": 4, "pitch": "C"}, "duration": 1.0},
                    {"pitch": {"octave": 4, "pitch": "E"}, "duration": 1.0}
                  ]
                },
                {
                  "source": "literal",
                  "literalNotes": [
                    {"pitch": {"octave": 4, "pitch": "F"}, "duration": 1.5},
                    {"pitch": {"octave": 4, "pitch": "E"}, "duration": 0.16666667},
                    {"pitch": {"octave": 4, "pitch": "D"}, "duration": 0.16666667},
                    {"pitch": {"octave": 4, "pitch": "C"}, "duration": 0.16666667},
                    {"pitch": {"octave": 3, "pitch": "B"}, "duration": 1.0},
                    {"rest": true, "duration": 0.75},
                    {"pitch": {"octave": 3, "pitch": "G"}, "duration": 0.25}
                  ]
                }
              ]
            },
            {
              "name": "Phrase2",
              "cadenceType": 2,
              "cadenceTarget": 2,
              "figures": [
                {
                  "source": "literal",
                  "literalNotes": [
                    {"pitch": {"octave": 3, "pitch": "B"}, "duration": 1.0},
                    {"pitch": {"octave": 4, "pitch": "D"}, "duration": 1.0},
                    {"pitch": {"octave": 4, "pitch": "F"}, "duration": 1.0},
                    {"pitch": {"octave": 4, "pitch": "D"}, "duration": 1.0}
                  ]
                },
                {
                  "source": "literal",
                  "literalNotes": [
                    {"pitch": {"octave": 4, "pitch": "G"}, "duration": 1.5},
                    {"pitch": {"octave": 4, "pitch": "A"}, "duration": 0.16666667},
                    {"pitch": {"octave": 4, "pitch": "G"}, "duration": 0.16666667},
                    {"pitch": {"octave": 4, "pitch": "F"}, "duration": 0.16666667},
                    {"pitch": {"octave": 4, "pitch": "E"}, "duration": 1.0},
                    {"rest": true, "duration": 1.0}
                  ]
                }
              ]
            }
          ]
        }
      }
    }
  ]
}
```

Notes:
- `Phrase2` does NOT have its own `startingPitch` — cursor inherits from phrase 1's ending (G3 after the pickup sixteenth, per the cursor-tracking math in Task 3). Its first literal figure's first note B3 has step = +2 (G3 → A3 → B3).
- `cadenceType` and `cadenceTarget` are set but inert because Task 4 skips cadence adjustment for Literal endings. They're included for documentation.
- `masterSeed` is 467 for amusement; the composition is fully prescribed so seed has no effect on output.
- The `bpm` is 100 (Allegro but on the slow side of Allegro maestoso, easier to hear the phrase shape in audit). Matt can adjust on return.

Before saving, cross-check every JSON field against the current loader in `templates_json.h`. The `FigureSource::Literal` enum value is spelled `"literal"` in JSON. The `pitch` object uses `{"octave": <int>, "pitch": <letter>}`. The `rest` field is `{"rest": true, "duration": <float>}`. The `startingPitch` on passage and phrase uses `{"octave": <int>, "pitch": <letter>}`. If any field name differs, fix the template to match.

- [ ] **Step 2: Verify the JSON loads**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/k467_smoke 1 --template patches/test_k467_opening.json
```

Expected stdout:
- `Loaded template: patches/test_k467_opening.json` (no parse errors)
- `Composed #1: renders/k467_smoke_1.wav (16 beats @ 100 bpm)`
- `peak=<X>` where X is between 0.05 and 1.0
- `rms=<Y>` where Y > 0.01
- `Saved: renders/k467_smoke_1.json`

**Failure modes to diagnose if the CLI errors out**:
- JSON parse error → field name mismatch. Cross-check with `templates_json.h` deserializers.
- Runtime crash → probably an access to `literalNotes[i].pitch` when the note is a rest; Task 2's Literal-case fix should guard this but may have been missed.
- Silent audio (rms < 0.005) → all notes are probably rests or otherwise broken; inspect `k467_smoke_1.json` to see what got composed.

- [ ] **Step 3: Inspect the composed JSON**

```
python -c "
import json
with open('renders/k467_smoke_1.json') as f:
    p = json.load(f)
for part in p['parts']:
    for pname, passage in part['passages'].items():
        print(f'Passage {pname}:')
        for pi, phr in enumerate(passage['phrases']):
            print(f'  phrase {pi}: startingPitch={phr.get(\"startingPitch\")}')
            for fi, fig in enumerate(phr['figures']):
                total_beats = sum(u['duration'] for u in fig['units'])
                print(f'    fig {fi}: {len(fig[\"units\"])} units, {total_beats:.3f} beats')
                for u in fig['units']:
                    print(f'      step={u[\"step\"]}, dur={u[\"duration\"]:.4f}, rest={u.get(\"rest\", False)}')
"
```

Expected:
- Phrase 0: startingPitch C4
  - fig 0: 4 units, 4.0 beats, steps `[0, -3, +3, +2]`, no rests
  - fig 1: 7 units, 4.0 beats, steps `[+1, -1, -1, -1, -1, 0, -2]`, unit 5 rest=true
- Phrase 1: startingPitch <cursor inherited; should show G3 from the end of fig 1 after Task 3's cursor tracking>
  - fig 0: 4 units, 4.0 beats, steps `[+2, +2, +2, -2]`
  - fig 1: 6 units, 4.0 beats, steps `[+3, +1, -1, -1, -1, 0]`, unit 5 rest=true

If the step values don't match (especially in fig 0 of phrase 0 where `-3, +3` is the diagnostic for octave-aware math), Task 1's fix didn't take effect. Diagnose before continuing.

If phrase 1's starting pitch shows C4 (instead of G3 via inheritance), Task 3's cursor tracking isn't actually updating `figCtx.cursor` or Phrase 2 is resetting. Diagnose.

The "phrase ending pitch" is harder to verify from this inspection; rely on the step math and cursor walking to reason about it. Alternatively, audit by ear (Matt's job).

- [ ] **Step 4: Clean up the scratch render but keep the template**

```
rm renders/k467_smoke_1.wav renders/k467_smoke_1.json 2>/dev/null
```

Keep `patches/test_k467_opening.json`.

- [ ] **Step 5: Commit the template**

```
git add patches/test_k467_opening.json
git commit -m "test(composer): K467/i opening 4-bar literal template

Mozart Piano Concerto No. 21 K467 mvt I opening, 4 bars in C major,
fully prescribed via FigureSource::Literal. Two phrases x two figures
each, exact pitches and durations from Matt's transcription:
  Phrase 1: C4 G3 C4 E4 | F4 E4 D4 C4 B3 rest G3 (half cadence ^7/V)
  Phrase 2: B3 D4 F4 D4 | G4 A4 G4 F4 E4 rest  (IAC ^3/I)

Exercises the Phase-1b Literal path end-to-end with the four fixes
from this plan applied. Not pinned as a golden — Matt auditions the
rendered WAV on return and decides whether the literal path reproduces
Mozart recognizably."
```

---

## Task 6: Final sweep

- [ ] **Step 1: Verify the committed golden still renders to its pinned hash**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/final_golden 1 --template patches/template_golden_phase1a.json
sha256sum renders/final_golden_1.wav
```

Expected: `0815d32ebc8ae34c79a4dc1b8c069fe88538fdc373b0e2cb3a934c1164a0f1ff`.

Also run the sha256 check file:
```
sha256sum -c renders/template_golden_phase1a.sha256
```
Must pass.

- [ ] **Step 2: Render the K467 smoke test one more time and confirm non-silent + non-crash**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/k467_final 1 --template patches/test_k467_opening.json
```

Expected: loads cleanly, renders 16 beats at 100 bpm, peak and rms in sensible ranges. Do NOT pin the hash — Matt auditions.

- [ ] **Step 3: Clean up scratch files**

```
rm renders/final_golden_1.wav renders/final_golden_1.json 2>/dev/null
rm renders/k467_final_1.wav renders/k467_final_1.json 2>/dev/null
```

No commit — the final-sweep tasks are verification only.

---

## Exit criteria

1. `cmake --build` succeeds from a clean tree.
2. Golden hash `0815d32e...` is unchanged after all 4 fixes.
3. `patches/test_k467_opening.json` exists on main and loads without parse errors.
4. Rendering the K467 template produces non-silent audio with sensible peak/rms.
5. Composed K467 JSON shows 2 phrases, 2 figures each, with the expected step sequences per the diagnostic in Task 5 Step 3.
6. `FigureTemplate::LiteralNote` has a `rest` field.
7. `DefaultFigureStrategy::realize_figure` Literal case uses octave-aware scale-degree math.
8. `DefaultPhraseStrategy::realize_phrase` maintains intra-phrase cursor tracking.
9. `DefaultPhraseStrategy::realize_phrase` skips `apply_cadence` for Literal/Locked last figures.
10. Commit log shows the 5 content commits in order: Task 1 fix → Task 2 fix → Task 3 fix → Task 4 fix → Task 5 template.

---

## What's out of scope (as a reminder)

- No changes to Period / Sentence strategy configs (still hold `MelodicFigure`, not `FigureTemplate` — Issue 1 from the K467 brainstorming).
- No Outline strategies.
- No audition — Matt does that when he gets back.
- No pinning of the K467 render as a golden.
- No migration of other broken templates (the Phase 5 rename left `template_mary.json`, `template_binary.json`, etc. broken, unchanged by this plan).
- No new composition-quality fixes (motif scaling, rhythm-variation tuning, etc.).
- No changes to `conductor.h` or `dun_parser.h`.
