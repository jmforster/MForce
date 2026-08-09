# Harmony-First Composition Pipeline

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make harmony the foundation of composition — chord progressions drive figure generation, producing harmonically grounded melodies instead of random scale wandering.

**Architecture:** Four levels of harmony (piece key, section key, key center, chord) flow from Section down to the PitchReader. A new Figure base class splits into MelodicFigure (scale-step) and ChordFigure (chord-tone). HarmonyComposer builds chord progressions on Sections. AlternatingFigureStrategy (AFS) generates ABAB phrases alternating chord-tone and scalar figures over a progression.

**Tech Stack:** C++ (headers-only, inline in `engine/include/mforce/music/`). No new .cpp files needed for CMake. JSON patches for smoke tests. CLI for render verification.

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Modify | `engine/include/mforce/music/structure.h` | Add `ChordProgression` and `KeyContext` to Section |
| Modify | `engine/include/mforce/music/figures.h` | Extract Figure base class; MelodicFigure and ChordFigure inherit from it |
| Modify | `engine/include/mforce/music/conductor.h` | Teach `perform_phrase` to handle ChordFigure (chord-tone stepping) |
| Modify | `engine/include/mforce/music/strategy.h` | Update `StrategyContext` to carry current chord + key context |
| Modify | `engine/include/mforce/music/templates.h` | Add `StepMode` to FigureTemplate |
| Create | `engine/include/mforce/music/harmony_composer.h` | HarmonyComposer — builds ChordProgressions for Sections |
| Create | `engine/include/mforce/music/alternating_figure_strategy.h` | AFS — ABAB chord-tone/scalar figure generation over a progression |
| Modify | `engine/include/mforce/music/composer.h` | Register AFS, wire HarmonyComposer into compose pipeline |
| Modify | `engine/include/mforce/music/templates_json.h` | JSON support for new fields (stepMode, chordProgression, keyContexts) |
| Create | `patches/test_k467_harmony.json` | Smoke test: K467 opening with harmony-driven composition |

---

### Task 1: Add ChordProgression and KeyContext to Section

**Files:**
- Modify: `engine/include/mforce/music/structure.h:106-116`

This wires the four levels of harmony into the data model. Section gains a chord progression (level 4) and a key context timeline (level 3).

- [ ] **Step 1: Add KeyContext struct and fields to Section**

In `structure.h`, above the Section struct, add:

```cpp
// ===========================================================================
// KeyContext — a temporary key center within a Section (level 3 harmony).
// The PitchReader switches to this key's scale at the given beat.
// ===========================================================================
struct KeyContext {
  float beat{0.0f};                    // beat within Section where this kicks in
  Key key;                             // implies a default scale
  std::optional<Scale> scaleOverride;  // if different from key's default scale

  Scale effective_scale() const {
    return scaleOverride.value_or(key.scale);
  }
};
```

Then add to the `Section` struct:

```cpp
struct Section {
  std::string name;
  float beats;
  Meter meter{Meter::M_4_4};
  float tempo{120.0f};
  Scale scale;

  // Harmony levels 3 and 4
  std::vector<KeyContext> keyContexts;          // level 3: transient key centers
  std::optional<ChordProgression> chordProgression;  // level 4: chord sequence

  Section() : scale(Scale::get("C", "Major")) {}
  Section(const std::string& n, float b, float bpm, Meter m, Scale s)
    : name(n), beats(b), meter(m), tempo(bpm), scale(s) {}

  // Get the active scale at a given beat (checks keyContexts, falls back to section scale)
  Scale active_scale_at(float beat) const {
    Scale result = scale;
    for (const auto& kc : keyContexts) {
      if (kc.beat <= beat) result = kc.effective_scale();
    }
    return result;
  }
};
```

Note: `#include "mforce/music/figures.h"` is already included transitively via the existing includes in structure.h. Verify this — if not, add it, since `ChordProgression` is defined in `figures.h`.

- [ ] **Step 2: Build and verify**

Run:
```bash
cmake --build build --target mforce_cli 2>&1 | head -30
```
Expected: Clean build. The new fields are optional/defaulted so nothing breaks.

- [ ] **Step 3: Commit**

```bash
git add engine/include/mforce/music/structure.h
git commit -m "feat(harmony): add ChordProgression and KeyContext to Section"
```

---

### Task 2: Extract Figure base class, create ChordFigure

**Files:**
- Modify: `engine/include/mforce/music/figures.h:468-565`
- Modify: `engine/include/mforce/music/structure.h:76-85` (Phrase's vector type)

The current `MelodicFigure` becomes a subclass of `Figure`. A new `ChordFigure` also inherits from `Figure`. Both carry the same `vector<FigureUnit>` and methods. `Phrase` holds `vector<unique_ptr<Figure>>`.

- [ ] **Step 1: Create Figure base class in figures.h**

Replace the current `MelodicFigure` struct (lines ~502-565) with:

```cpp
// ---------------------------------------------------------------------------
// Figure — base class for melodic content. Holds FigureUnits.
// Subclasses: MelodicFigure (scale-step), ChordFigure (chord-tone).
// ---------------------------------------------------------------------------
struct Figure {
  std::vector<FigureUnit> units;

  virtual ~Figure() = default;

  int note_count() const { return int(units.size()); }

  void set_articulation(int index, Articulation art) { units[index].articulation = art; }
  void set_ornament(int index, Ornament orn) { units[index].ornament = orn; }

  float total_duration() const {
    float t = 0;
    for (auto& u : units) t += u.duration;
    return t;
  }

  int net_step() const {
    int n = 0;
    for (int i = 0; i < int(units.size()); ++i) n += units[i].step;
    return n;
  }

  PulseSequence extract_pulses() const {
    PulseSequence ps;
    for (const auto& u : units) ps.add(u.duration);
    return ps;
  }

  StepSequence extract_steps() const {
    StepSequence ss;
    for (const auto& u : units) ss.add(u.step);
    return ss;
  }

  // Clone support for unique_ptr in containers
  virtual std::unique_ptr<Figure> clone() const = 0;
};

// ---------------------------------------------------------------------------
// MelodicFigure — steps are scale-degree movement.
// ---------------------------------------------------------------------------
struct MelodicFigure : Figure {
  MelodicFigure() = default;

  MelodicFigure(const PulseSequence& pulses, const StepSequence& steps) {
    if (steps.count() != pulses.count() - 1)
      throw std::runtime_error("MelodicFigure: step count must be pulse count - 1");
    for (int i = 0; i < pulses.count(); ++i) {
      FigureUnit u;
      u.duration = pulses.get(i);
      u.step = (i == 0) ? 0 : steps.get(i - 1);
      units.push_back(u);
    }
  }

  static MelodicFigure from_atoms(const PulseSequence& pulses, const StepSequence& steps) {
    MelodicFigure fig;
    int n = std::min(pulses.count(), steps.count());
    for (int i = 0; i < n; ++i) {
      FigureUnit u;
      u.duration = pulses.get(i);
      u.step = steps.get(i);
      fig.units.push_back(u);
    }
    return fig;
  }

  std::unique_ptr<Figure> clone() const override {
    auto c = std::make_unique<MelodicFigure>();
    c->units = units;
    return c;
  }
};

// ---------------------------------------------------------------------------
// ChordFigure — steps are chord-tone movement.
// The Conductor resolves these against the active chord at performance time.
// ---------------------------------------------------------------------------
struct ChordFigure : Figure {
  ChordFigure() = default;

  ChordFigure(const PulseSequence& pulses, const StepSequence& steps) {
    if (steps.count() != pulses.count() - 1)
      throw std::runtime_error("ChordFigure: step count must be pulse count - 1");
    for (int i = 0; i < pulses.count(); ++i) {
      FigureUnit u;
      u.duration = pulses.get(i);
      u.step = (i == 0) ? 0 : steps.get(i - 1);
      units.push_back(u);
    }
  }

  static ChordFigure from_atoms(const PulseSequence& pulses, const StepSequence& steps) {
    ChordFigure fig;
    int n = std::min(pulses.count(), steps.count());
    for (int i = 0; i < n; ++i) {
      FigureUnit u;
      u.duration = pulses.get(i);
      u.step = steps.get(i);
      fig.units.push_back(u);
    }
    return fig;
  }

  std::unique_ptr<Figure> clone() const override {
    auto c = std::make_unique<ChordFigure>();
    c->units = units;
    return c;
  }
};
```

- [ ] **Step 2: Update Phrase to hold unique_ptr\<Figure\>**

In `structure.h`, update the Phrase struct:

```cpp
struct Phrase {
  Pitch startingPitch;
  std::vector<std::unique_ptr<Figure>> figures;

  void add_figure(std::unique_ptr<Figure> fig) {
    figures.push_back(std::move(fig));
  }

  // Convenience: add a MelodicFigure by value (wraps in unique_ptr)
  void add_melodic_figure(MelodicFigure fig) {
    figures.push_back(std::make_unique<MelodicFigure>(std::move(fig)));
  }

  int figure_count() const { return int(figures.size()); }
};
```

Add `#include <memory>` to structure.h if not already present.

- [ ] **Step 3: Fix all call sites that use Phrase::add_figure or phrase.figures**

This is the ripple. Every place that currently does `phrase.add_figure(someGlobalMelodicFigure)` or iterates `phrase.figures[f]` needs updating.

**Conductor (`conductor.h:492-519`):** Update `perform_phrase` to dereference the unique_ptr:

Change:
```cpp
const auto& fig = phrase.figures[f];
```
To:
```cpp
const auto& fig = *phrase.figures[f];
```

The rest of the loop (iterating `fig.units`) works unchanged since `Figure` base has `units`.

**Composer and strategies:** Every strategy that builds a Phrase currently does `phrase.add_figure(melodicFigure)`. Change these to `phrase.add_melodic_figure(melodicFigure)` which wraps in unique_ptr automatically. Search for all occurrences:

```
grep -rn "add_figure" engine/include/mforce/music/
```

Update each call site. The `add_melodic_figure` convenience method handles the common case.

- [ ] **Step 4: Fix Passage copy semantics**

`Passage` contains `vector<Phrase>` and `Phrase` now has `unique_ptr` members, making it move-only. Verify that Passage and Phrase are only ever moved, not copied. If any copy is needed, add a deep-copy helper that uses `clone()`:

```cpp
// In Phrase:
Phrase deep_copy() const {
  Phrase p;
  p.startingPitch = startingPitch;
  for (const auto& f : figures) {
    p.figures.push_back(f->clone());
  }
  return p;
}
```

- [ ] **Step 5: Build and verify**

Run:
```bash
cmake --build build --target mforce_cli 2>&1 | head -50
```
Expected: Clean build. Fix any remaining call sites that the compiler flags.

- [ ] **Step 6: Run an existing patch to verify no regression**

```bash
./build/tools/mforce_cli/mforce_cli patches/test_k467_motifs.json
```
Expected: Renders successfully, output sounds the same as before.

- [ ] **Step 7: Commit**

```bash
git add engine/include/mforce/music/figures.h engine/include/mforce/music/structure.h engine/include/mforce/music/conductor.h engine/include/mforce/music/composer.h engine/include/mforce/music/default_strategies.h engine/include/mforce/music/phrase_strategies.h engine/include/mforce/music/shape_strategies.h
git commit -m "refactor(music): extract Figure base class, add ChordFigure for chord-tone movement"
```

---

### Task 3: Add StepMode to FigureTemplate

**Files:**
- Modify: `engine/include/mforce/music/templates.h:92-138`
- Modify: `engine/include/mforce/music/templates_json.h` (JSON deserialization)

- [ ] **Step 1: Add StepMode enum and field to FigureTemplate**

In `templates.h`, near the other enums at the top:

```cpp
enum class StepMode { Scale, ChordTone };
```

In `FigureTemplate`, add the field:

```cpp
StepMode stepMode{StepMode::Scale};  // how PitchReader interprets steps
```

- [ ] **Step 2: Add JSON support for stepMode**

In `templates_json.h`, in the FigureTemplate deserialization, add:

```cpp
if (j.contains("stepMode")) {
  auto sm = j["stepMode"].get<std::string>();
  if (sm == "chordTone") ft.stepMode = StepMode::ChordTone;
  else ft.stepMode = StepMode::Scale;  // default
}
```

- [ ] **Step 3: Build and verify**

Run:
```bash
cmake --build build --target mforce_cli 2>&1 | head -30
```
Expected: Clean build. No behavioral change yet — nothing reads `stepMode`.

- [ ] **Step 4: Commit**

```bash
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h
git commit -m "feat(harmony): add StepMode to FigureTemplate (Scale vs ChordTone)"
```

---

### Task 4: Teach Conductor to perform ChordFigures

**Files:**
- Modify: `engine/include/mforce/music/conductor.h:44-80,483-524`

The Conductor's `perform_phrase` currently calls `step_note()` for every FigureUnit, which navigates by scale degrees. For a `ChordFigure`, steps should navigate by chord tones instead. The Conductor needs the Section's chord progression to know what chord is active at each beat.

- [ ] **Step 1: Add chord-tone stepping function**

Near the existing `step_note` function (line 44), add:

```cpp
// Step through chord tones. The chord's pitches vector provides the available tones.
// steps > 0 = move up through chord tones, steps < 0 = move down.
// Wraps across octaves (chord tones repeat every octave).
inline float step_chord_tone(float noteNumber, int steps, const Chord& chord) {
    if (steps == 0 || chord.pitches.empty()) return noteNumber;

    // Build a sorted list of all chord tone note numbers across a wide range.
    // Chord.pitches gives us one octave's worth; we extend ±2 octaves.
    std::vector<float> tones;
    for (int octShift = -2; octShift <= 2; ++octShift) {
        for (const auto& p : chord.pitches) {
            tones.push_back(p.note_number() + 12.0f * octShift);
        }
    }
    std::sort(tones.begin(), tones.end());

    // Find the closest chord tone to current position
    int closest = 0;
    float minDist = 999.0f;
    for (int i = 0; i < int(tones.size()); ++i) {
        float d = std::abs(tones[i] - noteNumber);
        if (d < minDist) { minDist = d; closest = i; }
    }

    // If first step direction disagrees with snap direction, find chord tone in step direction
    if (steps > 0 && tones[closest] < noteNumber - 0.1f) {
        // Snapped below us but we want to go up — find next tone above
        for (int i = closest + 1; i < int(tones.size()); ++i) {
            if (tones[i] >= noteNumber - 0.1f) { closest = i; break; }
        }
    } else if (steps < 0 && tones[closest] > noteNumber + 0.1f) {
        // Snapped above us but we want to go down — find next tone below
        for (int i = closest - 1; i >= 0; --i) {
            if (tones[i] <= noteNumber + 0.1f) { closest = i; break; }
        }
    }

    // Now step through chord tones
    int target = closest + steps;
    target = std::max(0, std::min(target, int(tones.size()) - 1));
    return tones[target];
}
```

- [ ] **Step 2: Update perform_phrase to handle ChordFigure**

Modify `perform_phrase` to accept the Section's chord progression and check figure type. Update the signature:

```cpp
float perform_phrase(const Phrase& phrase, const Scale& scale,
                     float startBeat, float bpm,
                     DynamicState& dynamics,
                     const std::vector<DynamicMarking>& markings, int& nextMarking,
                     float passageBeatOffset,
                     PitchedInstrument& instrument,
                     const std::optional<ChordProgression>& chordProg = std::nullopt,
                     const Scale& sectionScale = Scale::get("C", "Major"),
                     int baseOctave = 4) {
```

In the inner loop, replace:

```cpp
currentNN = step_note(currentNN, u.step, scale);
```

With:

```cpp
// Check if this figure is a ChordFigure (chord-tone stepping)
if (dynamic_cast<const ChordFigure*>(phrase.figures[f].get()) && chordProg) {
    // Find the active chord at this beat
    float sectionBeat = currentBeat - passageBeatOffset;
    float chordBeat = 0;
    int chordIdx = 0;
    for (int ci = 0; ci < chordProg->count(); ++ci) {
        if (chordBeat + chordProg->pulses.get(ci) > sectionBeat) {
            chordIdx = ci;
            break;
        }
        chordBeat += chordProg->pulses.get(ci);
        chordIdx = ci;
    }
    // Resolve the active chord
    auto resolved = chordProg->chords.get(chordIdx).resolve(
        sectionScale, baseOctave);
    currentNN = step_chord_tone(currentNN, u.step, resolved);
} else {
    currentNN = step_note(currentNN, u.step, scale);
}
```

- [ ] **Step 3: Update perform_passage to pass chord progression through**

In `perform_passage`, where it calls `perform_phrase`, pass the Section's chord progression. This requires `perform_passage` to receive the Section (or at least its chord progression). Update the call chain from `perform(const Piece&)` downward to thread the Section through.

In the main `perform(const Piece& piece)` loop (around line 440), the Section is already available. Pass `section.chordProgression` down to `perform_passage` and then to `perform_phrase`.

- [ ] **Step 4: Build and verify**

Run:
```bash
cmake --build build --target mforce_cli 2>&1 | head -50
```
Expected: Clean build. Existing patches still work (they have no chord progressions, so the `chordProg` optional is empty and the else branch fires).

- [ ] **Step 5: Run regression test**

```bash
./build/tools/mforce_cli/mforce_cli patches/test_k467_motifs.json
```
Expected: Same output as before — no ChordFigures in this patch.

- [ ] **Step 6: Commit**

```bash
git add engine/include/mforce/music/conductor.h
git commit -m "feat(harmony): teach Conductor to perform ChordFigures via chord-tone stepping"
```

---

### Task 5: Update StrategyContext with harmonic state

**Files:**
- Modify: `engine/include/mforce/music/strategy.h:18-30`

Strategies need to know the current chord and key context to generate harmonically-aware figures.

- [ ] **Step 1: Add harmonic fields to StrategyContext**

```cpp
struct StrategyContext {
  Scale scale;
  Pitch cursor;
  float totalBeats{0.0f};
  Piece* piece{nullptr};
  const PieceTemplate* template_{nullptr};
  Composer* composer{nullptr};
  nlohmann::json params;
  Randomizer* rng{nullptr};

  // Harmony context
  const ChordProgression* chordProgression{nullptr};  // Section's progression
  const std::vector<KeyContext>* keyContexts{nullptr}; // Section's key centers
  float sectionBeatOffset{0.0f};                       // beat offset within section
};
```

- [ ] **Step 2: Wire harmonic context into StrategyContext in Composer**

In `composer.h`, in `compose_passage_` (where StrategyContext is constructed), populate the new fields from the Section:

```cpp
ctx.chordProgression = section.chordProgression ? &*section.chordProgression : nullptr;
ctx.keyContexts = section.keyContexts.empty() ? nullptr : &section.keyContexts;
```

- [ ] **Step 3: Build and verify**

Run:
```bash
cmake --build build --target mforce_cli 2>&1 | head -30
```
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add engine/include/mforce/music/strategy.h engine/include/mforce/music/composer.h
git commit -m "feat(harmony): add harmonic context (chords, key centers) to StrategyContext"
```

---

### Task 6: Create HarmonyComposer

**Files:**
- Create: `engine/include/mforce/music/harmony_composer.h`

A simple, throwaway-grade composer that builds chord progressions for Sections. For now, hard-coded common progressions that can be selected by name. This is trivial to replace later once we know what we want.

- [ ] **Step 1: Create harmony_composer.h**

```cpp
#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/core/randomizer.h"
#include <string>
#include <unordered_map>
#include <functional>

namespace mforce {

// ---------------------------------------------------------------------------
// HarmonyComposer — builds ChordProgressions for Sections.
// Throwaway-grade: hard-coded named progressions, easy to replace.
// ---------------------------------------------------------------------------
struct HarmonyComposer {

  // Build a named progression scaled to fit the given number of beats.
  // Returns a ChordProgression with ScaleChords (scale-relative, not resolved).
  static ChordProgression build(const std::string& progressionName, float totalBeats) {
    auto it = progressions().find(progressionName);
    if (it == progressions().end()) {
      throw std::runtime_error("HarmonyComposer: unknown progression '" + progressionName + "'");
    }
    return it->second(totalBeats);
  }

  // List available progression names
  static std::vector<std::string> available() {
    std::vector<std::string> names;
    for (const auto& [k, v] : progressions()) names.push_back(k);
    return names;
  }

private:
  using Builder = std::function<ChordProgression(float totalBeats)>;

  static const std::unordered_map<std::string, Builder>& progressions() {
    static const std::unordered_map<std::string, Builder> map = {

      // I - V7 - V7 - I  (K467 opening)
      {"I-V7-V7-I", [](float beats) {
        float bar = beats / 4.0f;
        ChordProgression prog;
        prog.add(0, "Major", bar);     // I
        prog.add(4, "7", bar);         // V7
        prog.add(4, "7", bar);         // V7
        prog.add(0, "Major", bar);     // I
        return prog;
      }},

      // I - IV - V - I  (basic classical)
      {"I-IV-V-I", [](float beats) {
        float bar = beats / 4.0f;
        ChordProgression prog;
        prog.add(0, "Major", bar);
        prog.add(3, "Major", bar);     // IV
        prog.add(4, "Major", bar);     // V
        prog.add(0, "Major", bar);     // I
        return prog;
      }},

      // I - V - vi - IV  (pop)
      {"I-V-vi-IV", [](float beats) {
        float bar = beats / 4.0f;
        ChordProgression prog;
        prog.add(0, "Major", bar);
        prog.add(4, "Major", bar);
        prog.add(5, "minor", bar);     // vi
        prog.add(3, "Major", bar);     // IV
        return prog;
      }},

      // ii - V - I  (jazz turnaround, 3 bars)
      {"ii-V-I", [](float beats) {
        float bar = beats / 3.0f;
        ChordProgression prog;
        prog.add(1, "minor", bar);     // ii
        prog.add(4, "7", bar);         // V7
        prog.add(0, "Major", bar);     // I
        return prog;
      }},

      // I - I - I - I  (single chord, for testing)
      {"I", [](float beats) {
        ChordProgression prog;
        prog.add(0, "Major", beats);
        return prog;
      }},
    };
    return map;
  }
};

} // namespace mforce
```

- [ ] **Step 2: Build and verify**

Run:
```bash
cmake --build build --target mforce_cli 2>&1 | head -30
```
Expected: Clean build (header isn't included anywhere yet, but verify syntax by temporarily including it in composer.h).

- [ ] **Step 3: Commit**

```bash
git add engine/include/mforce/music/harmony_composer.h
git commit -m "feat(harmony): add HarmonyComposer with named chord progressions"
```

---

### Task 7: Create AlternatingFigureStrategy (AFS)

**Files:**
- Create: `engine/include/mforce/music/alternating_figure_strategy.h`

AFS is a **Passage-level** strategy. It reads the Section's ChordProgression, takes two figure templates (A = chord-tone, B = scalar), and alternates them: A over chord 1, B over chord 2, A over chord 3, B over chord 4. Produces a single Phrase.

- [ ] **Step 1: Create alternating_figure_strategy.h**

```cpp
#pragma once
#include "mforce/music/strategy.h"
#include "mforce/music/figures.h"
#include "mforce/music/structure.h"
#include "mforce/music/templates.h"
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// AlternatingFigureStrategy (AFS) — Passage-level strategy.
// Alternates chord-tone (A) and scalar (B) figures over a chord progression.
// Expects the PassageTemplate to have exactly 1 PhraseTemplate with exactly
// 2 FigureTemplates: [0] = A template (chord-tone), [1] = B template (scalar).
// The progression comes from StrategyContext::chordProgression.
// ---------------------------------------------------------------------------
class AlternatingFigureStrategy : public Strategy {
public:
  std::string name() const override { return "alternating_figure"; }
  StrategyLevel level() const override { return StrategyLevel::Passage; }

  Passage realize_passage(const PassageTemplate& pt, StrategyContext& ctx) override {
    if (!ctx.chordProgression || ctx.chordProgression->count() == 0) {
      throw std::runtime_error("AFS: no chord progression in context");
    }
    if (pt.phrases.empty() || pt.phrases[0].figures.size() < 2) {
      throw std::runtime_error("AFS: need 1 phrase with at least 2 figure templates (A and B)");
    }

    const auto& chordProg = *ctx.chordProgression;
    const auto& figTemplateA = pt.phrases[0].figures[0];  // chord-tone
    const auto& figTemplateB = pt.phrases[0].figures[1];  // scalar

    Phrase phrase;
    phrase.startingPitch = pt.startingPitch.value_or(ctx.cursor);

    for (int ci = 0; ci < chordProg.count(); ++ci) {
      bool isA = (ci % 2 == 0);  // A on even bars, B on odd
      const auto& ft = isA ? figTemplateA : figTemplateB;

      // Build a FigureTemplate with the correct beat duration from the progression
      FigureTemplate adjusted = ft;
      adjusted.totalBeats = chordProg.pulses.get(ci);

      // Dispatch to the Composer's figure realization
      MelodicFigure rawFig = ctx.composer->realize_figure(adjusted, ctx);

      if (isA) {
        // Wrap as ChordFigure
        auto cf = std::make_unique<ChordFigure>();
        cf->units = std::move(rawFig.units);
        phrase.add_figure(std::move(cf));
      } else {
        // Wrap as MelodicFigure
        phrase.add_melodic_figure(std::move(rawFig));
      }

      // Advance cursor by the figure's net step (approximate — the Conductor
      // will do the real pitch tracking, but this keeps the context cursor
      // roughly in the right place for the next figure's generation).
      // For chord-tone figures this is imprecise but acceptable.
      int netStep = 0;
      const auto& lastFig = *phrase.figures.back();
      for (const auto& u : lastFig.units) netStep += u.step;
      for (int s = 0; s < std::abs(netStep); ++s) {
        if (netStep > 0)
          ctx.cursor = Pitch(ctx.cursor.note_number() + ctx.scale.ascending_step(0));
        else
          ctx.cursor = Pitch(ctx.cursor.note_number() - ctx.scale.ascending_step(0));
      }
    }

    Passage passage;
    passage.add_phrase(std::move(phrase));
    if (pt.dynamicMarkings.size() > 0) {
      passage.dynamicMarkings = pt.dynamicMarkings;
    }
    return passage;
  }
};

} // namespace mforce
```

Note: The cursor advancement is deliberately rough. The Conductor handles real pitch tracking. The strategy just needs to keep the cursor in the right neighborhood so subsequent figure generation starts from a sensible place.

- [ ] **Step 2: Build and verify**

Run:
```bash
cmake --build build --target mforce_cli 2>&1 | head -30
```
Expected: Clean build.

- [ ] **Step 3: Commit**

```bash
git add engine/include/mforce/music/alternating_figure_strategy.h
git commit -m "feat(harmony): add AlternatingFigureStrategy (AFS) for ABAB chord/scalar figures"
```

---

### Task 8: Register AFS and wire HarmonyComposer into pipeline

**Files:**
- Modify: `engine/include/mforce/music/composer.h`
- Modify: `engine/include/mforce/music/templates.h` (add progressionName to PieceTemplate/SectionDef)
- Modify: `engine/include/mforce/music/templates_json.h`

- [ ] **Step 1: Add progressionName to SectionDef in templates.h**

Find the `SectionDef` struct in templates.h and add:

```cpp
std::string progressionName;  // name for HarmonyComposer (empty = no progression)
```

- [ ] **Step 2: Add JSON support for progressionName**

In `templates_json.h`, in the SectionDef deserialization, add:

```cpp
if (j.contains("progressionName")) {
  sd.progressionName = j["progressionName"].get<std::string>();
}
```

- [ ] **Step 3: Register AFS in Composer constructor**

In `composer.h`, include the new headers:

```cpp
#include "mforce/music/harmony_composer.h"
#include "mforce/music/alternating_figure_strategy.h"
```

In the Composer constructor, register AFS:

```cpp
registry.register_strategy(std::make_unique<AlternatingFigureStrategy>());
```

- [ ] **Step 4: Wire HarmonyComposer into setup_piece_**

In `setup_piece_()`, where Sections are created from `SectionDef`s, add after section creation:

```cpp
if (!sd.progressionName.empty()) {
  section.chordProgression = HarmonyComposer::build(sd.progressionName, section.beats);
}
```

- [ ] **Step 5: Build and verify**

Run:
```bash
cmake --build build --target mforce_cli 2>&1 | head -30
```
Expected: Clean build. Existing patches have no `progressionName` so behavior unchanged.

- [ ] **Step 6: Commit**

```bash
git add engine/include/mforce/music/composer.h engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h
git commit -m "feat(harmony): register AFS, wire HarmonyComposer into composition pipeline"
```

---

### Task 9: JSON support for Section chord progressions and key contexts

**Files:**
- Modify: `engine/include/mforce/music/templates_json.h`
- Modify: `engine/include/mforce/music/music_json.h` (if ChordProgression/KeyContext serialization needed)

Full JSON round-trip support so chord progressions and key contexts can be specified directly in patch files (alternative to using `progressionName`).

- [ ] **Step 1: Add inline ChordProgression JSON support to SectionDef**

In `templates_json.h`, add support for a `chordProgression` array in section definitions:

```cpp
// In SectionDef deserialization:
if (j.contains("chordProgression")) {
  ChordProgression prog;
  for (const auto& entry : j["chordProgression"]) {
    int degree = entry["degree"].get<int>();
    std::string quality = entry.value("quality", "Major");
    float beats = entry["beats"].get<float>();
    prog.add(degree, quality, beats);
  }
  sd.chordProgression = prog;
}
```

Add a corresponding field to SectionDef:

```cpp
std::optional<ChordProgression> chordProgression;  // inline progression (overrides progressionName)
```

Update `setup_piece_` to prefer inline progression over named:

```cpp
if (sd.chordProgression) {
  section.chordProgression = *sd.chordProgression;
} else if (!sd.progressionName.empty()) {
  section.chordProgression = HarmonyComposer::build(sd.progressionName, section.beats);
}
```

- [ ] **Step 2: Add keyContexts JSON support**

```cpp
// In SectionDef deserialization:
if (j.contains("keyContexts")) {
  for (const auto& kc : j["keyContexts"]) {
    KeyContext ctx;
    ctx.beat = kc["beat"].get<float>();
    std::string keyName = kc["key"].get<std::string>();
    ctx.key = *Key::get(keyName);
    if (kc.contains("scaleOverride")) {
      auto root = kc["scaleOverride"]["root"].get<std::string>();
      auto type = kc["scaleOverride"]["type"].get<std::string>();
      ctx.scaleOverride = Scale::get(root, type);
    }
    sd.keyContexts.push_back(ctx);
  }
}
```

Add to SectionDef:

```cpp
std::vector<KeyContext> keyContexts;
```

Wire in `setup_piece_`:

```cpp
section.keyContexts = sd.keyContexts;
```

- [ ] **Step 3: Build and verify**

Run:
```bash
cmake --build build --target mforce_cli 2>&1 | head -30
```
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h engine/include/mforce/music/composer.h
git commit -m "feat(harmony): JSON support for inline chord progressions and key contexts"
```

---

### Task 10: K467 harmony smoke test

**Files:**
- Create: `patches/test_k467_harmony.json`

The payoff. A patch that uses AFS with the I-V7-V7-I progression to produce the K467 opening shape: arpeggiation (chord-tone) alternating with scalar cadential approaches.

- [ ] **Step 1: Create the test patch**

```json
{
  "_comment": "K467/i opening — harmony-first test. AFS alternates chord-tone arpeggiation (bars 1,3) with scalar cadential figures (bars 2,4) over I-V7-V7-I.",
  "keyName": "C",
  "scaleName": "Major",
  "bpm": 100.0,
  "masterSeed": 4670,
  "motifs": [
    {
      "name": "arpeggio_rhythm",
      "type": "rhythm",
      "rhythm": [1.0, 1.0, 1.0, 1.0],
      "userProvided": true
    },
    {
      "name": "cadence_rhythm",
      "type": "rhythm",
      "rhythm": [1.5, 0.16666667, 0.16666667, 0.16666667, 1.0, 1.0],
      "userProvided": true
    }
  ],
  "sections": [
    {
      "name": "Opening",
      "beats": 16,
      "progressionName": "I-V7-V7-I"
    }
  ],
  "parts": [
    {
      "name": "melody",
      "role": "melody",
      "passages": {
        "Opening": {
          "startingPitch": {"octave": 4, "pitch": "C"},
          "strategy": "alternating_figure",
          "phrases": [
            {
              "name": "Main",
              "startingPitch": {"octave": 4, "pitch": "C"},
              "figures": [
                {
                  "_comment": "A template: chord-tone arpeggiation",
                  "source": "generate",
                  "shape": "triadic_outline",
                  "rhythmMotifName": "arpeggio_rhythm",
                  "stepMode": "chordTone",
                  "totalBeats": 4.0
                },
                {
                  "_comment": "B template: scalar cadential approach",
                  "source": "generate",
                  "shape": "cadential_approach",
                  "rhythmMotifName": "cadence_rhythm",
                  "shapeDirection": -1,
                  "shapeParam": 5,
                  "totalBeats": 4.0
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

- [ ] **Step 2: Run the smoke test**

```bash
./build/tools/mforce_cli/mforce_cli patches/test_k467_harmony.json
```

Expected: Renders without crash. The output should contain alternating arpeggiated and scalar passages. Listen to the render — bars 1 and 3 should outline chord tones (C-E-G over I, G-B-D-F over V7), bars 2 and 4 should be stepwise scalar motion.

- [ ] **Step 3: Run the old K467 patch to verify no regression**

```bash
./build/tools/mforce_cli/mforce_cli patches/test_k467_motifs.json
```

Expected: Same output as before.

- [ ] **Step 4: Commit**

```bash
git add patches/test_k467_harmony.json
git commit -m "test(composer): K467 opening with harmony-first AFS composition"
```

---

## Implementation Notes

**What's deliberately left rough:**

- **HarmonyComposer** is just a lookup table of named progressions. Real harmonic composition (choosing progressions based on form, tension, etc.) comes later.
- **AFS** is rigid ABAB alternation. Real phrase structure will need more flexibility.
- **Cursor tracking in AFS** is approximate. The Conductor does the real pitch work.
- **step_chord_tone** builds chord tone arrays on each call. This is fine for offline composition but would need optimization for real-time.
- **KeyContext** support exists in the data model but the Conductor doesn't yet use `active_scale_at()` during performance. That's the next step after this works.

**What must work correctly:**

- ChordFigure steps resolve against chord tones, not scale degrees
- MelodicFigure steps still resolve against scale degrees (no regression)
- The chord progression on Section drives which chord is active at each beat
- Existing patches with no chord progression work identically to before
