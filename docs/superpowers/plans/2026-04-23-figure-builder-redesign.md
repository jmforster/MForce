# FigureBuilder Redesign Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the monolithic `FigureBuilder` struct with three purpose-driven pieces: `RandomFigureBuilder` (class, owns `PulseGenerator` + `StepGenerator`), `shape_figures::` (free functions, pure ordinal shapes), and `figure_transforms::` (free functions, operators on existing figures).

**Architecture:** Header-only components under `engine/include/mforce/music/`. RFB holds two generator objects; transforms are stateless with `Randomizer&` explicit when needed. All producers honor `units[0].step = 0` (the FigureConnector-required convention). Migration updates call sites in `templates.h`, `default_strategies.h`, `composer.h`; deletes `shape_strategies.h` (redundant) and the old `FigureBuilder` struct in `figures.h`.

**Tech Stack:** C++20, MSVC, CMake. Header-only pattern (existing convention in `engine/include/mforce/music/*.h`). No unit-test framework — validation discipline is build-clean + render + compare to baseline per `CLAUDE.md`.

**Spec:** `docs/superpowers/specs/2026-04-23-figure-builder-redesign-design.md`.

---

## File structure

**New files (header-only):**
- `engine/include/mforce/music/figure_constraints.h` — `Constraints` struct.
- `engine/include/mforce/music/shape_figures.h` — `shape_figures::` namespace, 6 pure shape templates.
- `engine/include/mforce/music/figure_transforms.h` — `figure_transforms::` namespace, 11 deterministic + 3 micro-insert + 3 randomized functions.
- `engine/include/mforce/music/random_figure_builder.h` — `RandomFigureBuilder` class.

**Modified files:**
- `engine/include/mforce/music/figures.h` — add `MelodicFigure::from_steps` static helper; later, delete `FigureBuilder` struct (Task 14).
- `engine/include/mforce/music/figures.h` — extend `PulseGenerator` with `generate_count`.
- `engine/include/mforce/music/templates.h` — update `TransformOp` cases (lines 563-614) to use `figure_transforms::`.
- `engine/include/mforce/music/default_strategies.h` — migrate `DefaultFigureStrategy` (lines 55-145) to use RFB + `shape_figures::` + `figure_transforms::`.
- `engine/include/mforce/music/composer.h` — migrate the one fallback site (line 982) to use RFB.

**Deleted files:**
- `engine/include/mforce/music/shape_strategies.h` — 16 shape-wrapper `FigureStrategy` classes, redundant once `shape_figures::` exists.

**Regenerated artifacts (expected drift):**
- `renders/test_k467_period_1.{wav,json}` and its siblings — the pulse-alphabet change + seed-lineage change guarantee bit-different output. Matt has confirmed this is acceptable.

---

## Build commands

From repo root, using the existing MSVC CMake setup:

```bash
cmake --build build --config Debug --target mforce_engine
cmake --build build --config Debug --target mforce_cli
```

Expected on success: non-zero exit only on compile error. Warnings from `/W4 /permissive-` are tolerated unless new.

**Render command** (for validation):

```bash
build/x64/Debug/mforce_cli.exe --compose patches/test_k467_period.json renders/test_k467_period.wav 100 --save-rendered renders/
```

Produces `renders/test_k467_period_1.{wav,json}` (plus `_2`, `_3` for multi-Part pieces). JSON is the score; WAV is the audio. Compare JSON byte-for-byte against the prior baseline for structural drift; compare WAV via ear or a short diff script.

---

## Phase 1 — Foundation

### Task 1: Constraints struct

**Files:**
- Create: `engine/include/mforce/music/figure_constraints.h`

- [ ] **Step 1: Create the header.**

```cpp
#pragma once
#include <optional>

namespace mforce {

// All constraint axes for figure generation. Fields are optional — only
// set what you want to pin down. RandomFigureBuilder satisfies every set
// constraint or throws.
struct Constraints {
  std::optional<int>   count;          // number of FigureUnits
  std::optional<float> length;         // total beats
  std::optional<int>   net;            // net step movement (sum of steps)
  std::optional<int>   ceiling;        // running step-position ceiling
  std::optional<int>   floor;          // running step-position floor
  std::optional<float> defaultPulse;   // bias center for pulse generator
  std::optional<float> minPulse;       // smallest permitted pulse
  std::optional<float> maxPulse;       // largest permitted pulse
  // future: preferStepwise, preferSkips, maxLeap, maxStep, targetContour
};

} // namespace mforce
```

- [ ] **Step 2: Verify it compiles in isolation.**

Run:
```bash
cmake --build build --config Debug --target mforce_engine
```
Expected: clean build; no file consumes this yet, but it's in the include path so the engine TU compiles fine.

- [ ] **Step 3: Commit.**

```bash
git add engine/include/mforce/music/figure_constraints.h
git commit -m "feat(figures): add Constraints struct for figure generation"
```

---

### Task 2: Extend PulseGenerator with generate_count

**Files:**
- Modify: `engine/include/mforce/music/figures.h:386-460` (existing `PulseGenerator` struct)

- [ ] **Step 1: Add `generate_count` method to `PulseGenerator`.**

Inside `struct PulseGenerator` in `figures.h`, after the existing `generate(float totalBeats, float defaultPulse = 1.0f)` method, add:

```cpp
// Generate a PulseSequence of exactly `count` durations, drawn from the
// same binary+triplet alphabet as generate(), weighted toward
// defaultPulse. Use case: RandomFigureBuilder::build_by_count when no
// total-beats constraint is set.
PulseSequence generate_count(int count, float defaultPulse = 1.0f) {
  PulseSequence ps;
  if (count <= 0) return ps;

  static const float BINARY[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
  // Weight each alphabet entry toward defaultPulse.
  std::vector<float> weights;
  float weightSum = 0;
  for (float d : BINARY) {
    float w = 1.0f / (1.0f + std::abs(d - defaultPulse) * 2.0f);
    weights.push_back(w);
    weightSum += w;
  }

  for (int i = 0; i < count; ++i) {
    float pick = rng.value() * weightSum;
    float accum = 0;
    int idx = 0;
    for (int j = 0; j < int(weights.size()); ++j) {
      accum += weights[j];
      if (accum >= pick) { idx = j; break; }
    }
    ps.add(BINARY[idx]);
  }

  return ps;
}
```

Notes: round 1 omits triplet groups from `generate_count` (they'd require a different unit-count accounting). Add them later if needed.

- [ ] **Step 2: Verify it builds.**

Run:
```bash
cmake --build build --config Debug --target mforce_engine
```
Expected: clean build. No callers yet.

- [ ] **Step 3: Commit.**

```bash
git add engine/include/mforce/music/figures.h
git commit -m "feat(figures): add PulseGenerator::generate_count for note-count-driven rhythm"
```

---

### Task 3: MelodicFigure::from_steps helper

**Files:**
- Modify: `engine/include/mforce/music/figures.h` (inside `struct MelodicFigure : Figure`, around line 542)

- [ ] **Step 1: Add the static helper.**

Inside `struct MelodicFigure : Figure`, add:

```cpp
// Build a MelodicFigure from a StepSequence with uniform pulse duration.
// Replaces the old FigureBuilder::build(StepSequence, pulse) method. The
// first unit's step is honored from ss[0] (caller responsible for the
// step[0]=0 convention if needed).
static MelodicFigure from_steps(const StepSequence& ss, float pulse) {
  PulseSequence ps;
  for (int i = 0; i < ss.count(); ++i) ps.add(pulse);
  return MelodicFigure(ps, ss);
}
```

Place it near the other constructors/factories on `MelodicFigure`.

- [ ] **Step 2: Verify it builds.**

Run:
```bash
cmake --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 3: Commit.**

```bash
git add engine/include/mforce/music/figures.h
git commit -m "feat(figures): add MelodicFigure::from_steps static helper"
```

---

## Phase 2 — ShapeFigures

### Task 4: Create shape_figures.h with all 6 shapes

**Files:**
- Create: `engine/include/mforce/music/shape_figures.h`

- [ ] **Step 1: Write the full header.**

```cpp
#pragma once
#include "mforce/music/figures.h"
#include <cmath>

namespace mforce::shape_figures {

// Pure ordinal shape templates — no state, no rng, no walker assumptions.
// All shapes honor the bible rule: units[0].step == 0.

// run(direction, count, pulse): consecutive same-direction steps.
// direction: positive = ascending, negative = descending.
inline MelodicFigure run(int direction, int count, float pulse = 1.0f) {
  if (count < 1) count = 1;
  int dir = (direction >= 0) ? 1 : -1;
  MelodicFigure fig;
  fig.units.push_back({pulse, 0});                    // starting unit
  for (int i = 1; i < count; ++i)
    fig.units.push_back({pulse, dir});
  return fig;
}

// repeats(count, pulse): same pitch N times.
inline MelodicFigure repeats(int count, float pulse = 1.0f) {
  if (count < 1) count = 1;
  MelodicFigure fig;
  for (int i = 0; i < count; ++i)
    fig.units.push_back({pulse, 0});
  return fig;
}

// neighbor(upper, pulse, doublePulseMain): main-neighbor-return (3 units).
// upper=true → 0,+1,-1;  false → 0,-1,+1.
// doublePulseMain: first and last units are 2*pulse.
inline MelodicFigure neighbor(bool upper, float pulse = 1.0f,
                              bool doublePulseMain = false) {
  int dir = upper ? 1 : -1;
  float mainPulse = doublePulseMain ? pulse * 2.0f : pulse;
  MelodicFigure fig;
  fig.units.push_back({mainPulse, 0});                // main
  fig.units.push_back({pulse, dir});                  // neighbor
  fig.units.push_back({mainPulse, -dir});             // return
  return fig;
}

// leap_and_fill(leapSize, leapUp, fillSteps, pulse): big step + stepwise
// recovery. Emits 1 + 1 + fillSteps units. fillSteps=0 defaults to
// leapSize-1 (full fill). leapSize is the magnitude of the leap step.
inline MelodicFigure leap_and_fill(int leapSize, bool leapUp,
                                   int fillSteps = 0, float pulse = 1.0f) {
  if (leapSize < 2) leapSize = 2;
  if (fillSteps <= 0) fillSteps = leapSize - 1;
  int leapDir = leapUp ? 1 : -1;
  int fillDir = -leapDir;
  MelodicFigure fig;
  fig.units.push_back({pulse, 0});                    // starting
  fig.units.push_back({pulse, leapSize * leapDir});   // leap
  for (int i = 0; i < fillSteps; ++i)
    fig.units.push_back({pulse, fillDir});            // stepwise recovery
  return fig;
}

// arc(direction, extent, returnExtent, pulse): arch or inverted arch.
// 1 + extent + returnExtent units. returnExtent=0 → full return
// (returnExtent = extent).
inline MelodicFigure arc(int direction, int extent,
                         int returnExtent = 0, float pulse = 1.0f) {
  if (extent < 1) extent = 1;
  if (returnExtent <= 0) returnExtent = extent;
  int dir = (direction >= 0) ? 1 : -1;
  MelodicFigure fig;
  fig.units.push_back({pulse, 0});                    // starting
  for (int i = 0; i < extent; ++i)
    fig.units.push_back({pulse, dir});                // outward
  for (int i = 0; i < returnExtent; ++i)
    fig.units.push_back({pulse, -dir});               // return
  return fig;
}

// zigzag(direction, cycles, stepSize, skipSize, pulse):
// step stepSize*dir, then -skipSize*dir, per cycle. 1 + 2*cycles units.
inline MelodicFigure zigzag(int direction, int cycles,
                            int stepSize = 2, int skipSize = 1,
                            float pulse = 1.0f) {
  if (cycles < 1) cycles = 1;
  int dir = (direction >= 0) ? 1 : -1;
  MelodicFigure fig;
  fig.units.push_back({pulse, 0});                    // starting
  for (int i = 0; i < cycles; ++i) {
    fig.units.push_back({pulse, stepSize * dir});
    fig.units.push_back({pulse, -skipSize * dir});
  }
  return fig;
}

} // namespace mforce::shape_figures
```

- [ ] **Step 2: Verify it builds.**

Run:
```bash
cmake --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 3: Sanity-check step[0]=0 convention.**

Add a tiny local compile-time check as a separate translation-unit test: create `/tmp/shape_figures_check.cpp` (not committed), include the header, instantiate each shape, and verify `fig.units[0].step == 0` for all 6. Run once locally. Delete after.

```cpp
// /tmp/shape_figures_check.cpp — throwaway
#include "mforce/music/shape_figures.h"
#include <cassert>
int main() {
  using namespace mforce::shape_figures;
  assert(run(+1, 4).units[0].step == 0);
  assert(repeats(4).units[0].step == 0);
  assert(neighbor(true).units[0].step == 0);
  assert(leap_and_fill(3, true).units[0].step == 0);
  assert(arc(+1, 3).units[0].step == 0);
  assert(zigzag(+1, 2).units[0].step == 0);
  return 0;
}
```

Optional — skip if build already passes and you trust the code. It's a one-off verification, not checked in.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/shape_figures.h
git commit -m "feat(figures): shape_figures namespace with 6 pure shape templates

Culled from 14 on FigureBuilder: removed triadic_outline, fanfare
(walker-specific), held_note (redundant with build_singleton),
and cadential_approach/anacrusis/sigh/suspension/cambiata (phrase-level
intent better handled via Constraints.net).

Renames:
  scalar_run    -> run
  scalar_return -> arc
  repeated_note -> repeats
  neighbor_tone -> neighbor

All shapes honor units[0].step=0."
```

---

## Phase 3 — FigureTransforms

### Task 5: Create figure_transforms.h skeleton + invert / retrograde_steps

**Files:**
- Create: `engine/include/mforce/music/figure_transforms.h`

- [ ] **Step 1: Write the header skeleton with the first two functions.**

```cpp
#pragma once
#include "mforce/music/figures.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <vector>

namespace mforce::figure_transforms {

// Deterministic transforms are pure. Randomized transforms take
// Randomizer& explicitly. All functions take const MelodicFigure& and
// return a new MelodicFigure — no mutation.

// --- Deterministic (no rng) ---

// invert(fig): negate all steps. units[0].step=0 is preserved.
inline MelodicFigure invert(const MelodicFigure& fig) {
  MelodicFigure out = fig;
  for (auto& u : out.units) u.step = -u.step;
  return out;
}

// retrograde_steps(fig): reverse pitch sequence in time. Pulses DO NOT
// reverse (hence the _steps suffix). Algorithm: new units[0].step=0;
// for i>=1, new step[i] = -(old step[count-i]). Pulses keep order.
// Example: [0,+1,+1,-2] -> [0,+2,-1,-1].
inline MelodicFigure retrograde_steps(const MelodicFigure& fig) {
  MelodicFigure out;
  const int n = int(fig.units.size());
  if (n == 0) return out;
  out.units.reserve(n);
  out.units.push_back({fig.units[0].duration, 0});
  for (int i = 1; i < n; ++i) {
    out.units.push_back({fig.units[i].duration, -fig.units[n - i].step});
  }
  return out;
}

} // namespace mforce::figure_transforms
```

- [ ] **Step 2: Verify it builds.**

Run:
```bash
cmake --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 3: Commit.**

```bash
git add engine/include/mforce/music/figure_transforms.h
git commit -m "feat(figures): figure_transforms namespace with invert + retrograde_steps

retrograde_steps replaces the old FigureBuilder::reverse which had a
misleading comment about sign-flip but didn't actually do it."
```

---

### Task 6: Add combine (both overloads) and replicate (both overloads)

**Files:**
- Modify: `engine/include/mforce/music/figure_transforms.h`

- [ ] **Step 1: Add adjust_last_pulse, set_last_pulse, and prune first** (combine depends on them).

Inside the namespace, before `invert`, add:

```cpp
// prune(fig, count, from_start): remove `count` units from the end
// (default) or the start. When from_start=true, the new first unit's
// step value is forced to 0 to honor the step[0]=0 convention (the
// original step value is lost).
inline MelodicFigure prune(const MelodicFigure& fig, int count,
                           bool from_start = false) {
  MelodicFigure out = fig;
  const int n = int(out.units.size());
  if (count < 0 || count >= n)
    throw std::invalid_argument("prune: count out of range");
  if (from_start) {
    out.units.erase(out.units.begin(), out.units.begin() + count);
    if (!out.units.empty()) out.units[0].step = 0;
  } else {
    out.units.resize(n - count);
  }
  return out;
}

// set_last_pulse(fig, duration): set last unit's duration absolutely.
inline MelodicFigure set_last_pulse(const MelodicFigure& fig,
                                    float duration) {
  MelodicFigure out = fig;
  if (!out.units.empty()) out.units.back().duration = duration;
  return out;
}

// adjust_last_pulse(fig, delta): change last unit's duration by delta
// (signed). Clamps at 0 to avoid negative durations.
inline MelodicFigure adjust_last_pulse(const MelodicFigure& fig,
                                       float delta) {
  MelodicFigure out = fig;
  if (!out.units.empty())
    out.units.back().duration = std::max(0.0f,
        out.units.back().duration + delta);
  return out;
}
```

- [ ] **Step 2: Add combine (canonical + sugar overload).**

After the above, still inside the namespace:

```cpp
// combine(a, b, fc): canonical join with a FigureConnector.
// Composes: prune(elideCount) -> adjust_last_pulse(adjustCount) ->
// set b.units[0].step = leadStep -> concatenate.
inline MelodicFigure combine(const MelodicFigure& a,
                             const MelodicFigure& b,
                             const FigureConnector& fc = {}) {
  MelodicFigure left = a;
  if (fc.elideCount > 0) left = prune(left, fc.elideCount);
  if (fc.adjustCount != 0.0f) left = adjust_last_pulse(left, fc.adjustCount);

  MelodicFigure right = b;
  if (!right.units.empty()) right.units[0].step = fc.leadStep;

  MelodicFigure out = left;
  for (const auto& u : right.units) out.units.push_back(u);
  return out;
}

// combine(a, b, leadStep, elide): sugar for the common case.
// elide=true means elide 1 unit.
inline MelodicFigure combine(const MelodicFigure& a,
                             const MelodicFigure& b,
                             int leadStep, bool elide = false) {
  FigureConnector fc;
  fc.leadStep = leadStep;
  fc.elideCount = elide ? 1 : 0;
  return combine(a, b, fc);
}
```

- [ ] **Step 3: Add replicate (both overloads).**

```cpp
// replicate(fig, repeats, leadStep, elide): iterative combine.
// Returns `repeats` copies of fig joined with (leadStep, elide) each.
inline MelodicFigure replicate(const MelodicFigure& fig, int repeats,
                               int leadStep = 0, bool elide = false) {
  if (repeats < 1) repeats = 1;
  MelodicFigure out = fig;
  for (int i = 1; i < repeats; ++i) out = combine(out, fig, leadStep, elide);
  return out;
}

// replicate(fig, connectorSteps): total copies = 1 + connectorSteps.size().
// connectorSteps[i] is the leadStep joining copy (i+1) to what precedes.
inline MelodicFigure replicate(const MelodicFigure& fig,
                               const std::vector<int>& connectorSteps) {
  MelodicFigure out = fig;
  for (int leadStep : connectorSteps) out = combine(out, fig, leadStep, false);
  return out;
}
```

- [ ] **Step 4: Build + commit.**

```bash
cmake --build build --config Debug --target mforce_engine
git add engine/include/mforce/music/figure_transforms.h
git commit -m "feat(figures): figure_transforms prune / set+adjust_last_pulse / combine / replicate

combine composes atomically from prune + adjust_last_pulse + leadStep
assignment + concat."
```

---

### Task 7: Add specialty + micro-insertions

**Files:**
- Modify: `engine/include/mforce/music/figure_transforms.h`

- [ ] **Step 1: Add replicate_and_prune.**

Inside the namespace, after `replicate`:

```cpp
// replicate_and_prune(fig, connectorSteps, pruneAt1, pruneAt2):
// Replicate per connectorSteps (total copies = 1 + size), then prune
// the last unit from the copy at 1-indexed position pruneAt1 and,
// if pruneAt2 != 0, also from pruneAt2. Value 0 = no prune at that slot.
// Use case: acceleration toward a climax.
inline MelodicFigure replicate_and_prune(
    const MelodicFigure& fig,
    const std::vector<int>& connectorSteps,
    int pruneAt1,
    int pruneAt2 = 0) {
  const int copies = 1 + int(connectorSteps.size());
  const int unitsPerCopy = int(fig.units.size());
  auto validate = [&](int idx) {
    if (idx < 0 || idx > copies)
      throw std::invalid_argument("replicate_and_prune: prune index out of range");
    if (idx > 0 && unitsPerCopy < 2)
      throw std::invalid_argument("replicate_and_prune: copy would be emptied by prune");
  };
  validate(pruneAt1);
  validate(pruneAt2);

  // Build copy-by-copy so we can prune the last unit of specific copies
  // before continuing the chain.
  MelodicFigure out;
  auto appendCopy = [&](const MelodicFigure& src, int leadStep) {
    MelodicFigure piece = src;
    if (!piece.units.empty()) piece.units[0].step = leadStep;
    for (const auto& u : piece.units) out.units.push_back(u);
  };
  // Copy 1
  appendCopy(fig, 0);
  if (pruneAt1 == 1 || pruneAt2 == 1) out.units.pop_back();
  // Copies 2..copies
  for (int i = 0; i < int(connectorSteps.size()); ++i) {
    int copyIdx = i + 2;
    appendCopy(fig, connectorSteps[i]);
    if (pruneAt1 == copyIdx || pruneAt2 == copyIdx) out.units.pop_back();
  }
  return out;
}
```

- [ ] **Step 2: Add split, add_neighbor, add_turn.**

```cpp
// split(fig, splitAt, repeats): replace unit at splitAt with `repeats`
// sub-units each of duration original.duration/repeats. First sub-unit
// inherits step; the rest have step=0. Length preserved; unit count
// grows by repeats-1.
inline MelodicFigure split(const MelodicFigure& fig, int splitAt,
                           int repeats) {
  if (repeats < 2 || splitAt < 0 || splitAt >= int(fig.units.size()))
    throw std::invalid_argument("split: invalid args");
  MelodicFigure out;
  out.units.reserve(fig.units.size() + repeats - 1);
  for (int i = 0; i < int(fig.units.size()); ++i) {
    if (i != splitAt) {
      out.units.push_back(fig.units[i]);
    } else {
      const auto& src = fig.units[i];
      float sub = src.duration / float(repeats);
      out.units.push_back({sub, src.step});      // first inherits step
      for (int k = 1; k < repeats; ++k)
        out.units.push_back({sub, 0});            // rest step=0
    }
  }
  return out;
}

// add_neighbor(fig, addAt, down): replace unit at addAt with a 3-unit
// neighbor motion of equal total duration:
//   sub-unit 0: duration/2, original step   (arrives at same pitch)
//   sub-unit 1: duration/4, step=down?-1:+1 (neighbor)
//   sub-unit 2: duration/4, step=down?+1:-1 (return)
// Length preserved; unit count grows by 2.
inline MelodicFigure add_neighbor(const MelodicFigure& fig, int addAt,
                                  bool down = false) {
  if (addAt < 0 || addAt >= int(fig.units.size()))
    throw std::invalid_argument("add_neighbor: addAt out of range");
  MelodicFigure out;
  out.units.reserve(fig.units.size() + 2);
  for (int i = 0; i < int(fig.units.size()); ++i) {
    if (i != addAt) {
      out.units.push_back(fig.units[i]);
    } else {
      const auto& src = fig.units[i];
      float halfDur = src.duration * 0.5f;
      float quarterDur = src.duration * 0.25f;
      int neighborStep = down ? -1 : +1;
      out.units.push_back({halfDur,    src.step});
      out.units.push_back({quarterDur, neighborStep});
      out.units.push_back({quarterDur, -neighborStep});
    }
  }
  return out;
}

// add_turn(fig, addAt, down): replace unit at addAt with a 4-unit turn
// motion of equal total duration. Round-1: upper/lower neighbor pair
// around the original pitch. down=false → upper-first (+1,-2,+1,0),
// down=true → lower-first (-1,+2,-1,0). Each sub-unit is duration/4.
// Length preserved; unit count grows by 3.
inline MelodicFigure add_turn(const MelodicFigure& fig, int addAt,
                              bool down = false) {
  if (addAt < 0 || addAt >= int(fig.units.size()))
    throw std::invalid_argument("add_turn: addAt out of range");
  MelodicFigure out;
  out.units.reserve(fig.units.size() + 3);
  for (int i = 0; i < int(fig.units.size()); ++i) {
    if (i != addAt) {
      out.units.push_back(fig.units[i]);
    } else {
      const auto& src = fig.units[i];
      float q = src.duration * 0.25f;
      int first = down ? -1 : +1;
      // (original step) -> upper/lower -> back-and-across -> return -> main
      out.units.push_back({q, src.step});       // arrive at main pitch
      out.units.push_back({q, first});          // upper (or lower) neighbor
      out.units.push_back({q, -2 * first});     // cross to other neighbor
      out.units.push_back({q, first});          // return to main
    }
  }
  return out;
}
```

- [ ] **Step 3: Build + commit.**

```bash
cmake --build build --config Debug --target mforce_engine
git add engine/include/mforce/music/figure_transforms.h
git commit -m "feat(figures): figure_transforms replicate_and_prune + split + add_neighbor + add_turn

Micro-insertions (split/add_neighbor/add_turn) are building blocks for
complexify/vary. replicate_and_prune is the specialty acceleration-toward-
climax composite. add_turn collides in name with the Turn ornament type;
different layer — ornament is metadata, add_turn inserts motion."
```

---

### Task 8: Add randomized transforms (vary, vary_rhythm, vary_steps, complexify, embellish)

**Files:**
- Modify: `engine/include/mforce/music/figure_transforms.h`

**Spec gap callout:** The spec has only `vary(fig, rng, amount)`. The old code calls `vary_rhythm` and `vary_steps` as distinct TransformOps. Migration needs them. Plan adds all three: `vary_rhythm`, `vary_steps`, and the consolidated `vary`. The two legacy variants preserve migration fidelity; the consolidated `vary` is the new single-entry catch-all.

- [ ] **Step 1: Add `vary_rhythm` (legacy behavior).**

Inside the namespace, before the deterministic section ends (or in a new `// Randomized` section):

```cpp
// --- Randomized (Randomizer& passed in) ---

// vary_rhythm(fig, rng): probability-weighted pulse split or dot.
// Legacy behavior from FigureBuilder::vary_rhythm.
inline MelodicFigure vary_rhythm(const MelodicFigure& fig, Randomizer& rng) {
  MelodicFigure out = fig;
  for (int x = 0; x < out.note_count() - 1; ++x) {
    if (rng.decide(0.2f)) {
      float dur = out.units[x].duration;
      if (dur < 0.5f * 2.0f) continue;            // min-pulse gate
      float dur1, dur2;
      if (dur < 1.0f || rng.decide(0.5f)) {
        dur1 = dur * 0.5f; dur2 = dur * 0.5f;     // even split
      } else {
        dur1 = dur * 0.75f; dur2 = dur * 0.25f;   // dotted split
      }
      out.units[x].duration = dur1;
      FigureUnit newUnit{dur2, 0};
      out.units.insert(out.units.begin() + x + 1, newUnit);
      break;
    } else if (x < out.note_count() - 1 && rng.decide(0.3f)) {
      float dur = out.units[x].duration;
      out.units[x].duration = dur * 1.5f;
      out.units[x + 1].duration = dur * 0.5f;
      break;
    }
  }
  return out;
}
```

- [ ] **Step 2: Add `vary_steps` (legacy behavior).**

```cpp
// vary_steps(fig, rng, variations): perturb `variations` interior steps.
// Each perturbation picks an interior index and adds a random int in
// [-2,+2] (guaranteed non-zero). Legacy from FigureBuilder::vary_steps.
inline MelodicFigure vary_steps(const MelodicFigure& fig, Randomizer& rng,
                                int variations = 1) {
  MelodicFigure out = fig;
  for (int i = 0; i < variations && out.note_count() > 1; ++i) {
    int idx = rng.int_range(1, out.note_count() - 2);
    int delta = rng.int_range(-2, 2);
    if (delta == 0) delta = (rng.int_range(0, 1) == 0) ? -1 : 1;
    out.units[idx].step += delta;
  }
  return out;
}
```

- [ ] **Step 3: Add consolidated `vary` (new).**

```cpp
// vary(fig, rng, amount): consolidated jitter. Applies a rhythm
// perturbation and a step perturbation, both scaled by `amount` in
// [0,1]. Round-1 implementation: probability of applying each sub-
// perturbation = amount; if applied, uses vary_rhythm / vary_steps
// atoms internally. Conservative by default.
inline MelodicFigure vary(const MelodicFigure& fig, Randomizer& rng,
                          float amount) {
  MelodicFigure out = fig;
  if (rng.decide(amount)) out = vary_rhythm(out, rng);
  if (rng.decide(amount)) out = vary_steps(out, rng, 1);
  return out;
}
```

- [ ] **Step 4: Add `complexify`.**

```cpp
// complexify(fig, rng, amount): length preserved; target unit count ≈
// (1+amount)*original. Round-1 implementation: repeatedly apply one of
// {split, add_neighbor, add_turn} at a random position until target
// reached or max iterations. amount=0 → no-op; amount=1 → doubled.
inline MelodicFigure complexify(const MelodicFigure& fig, Randomizer& rng,
                                float amount) {
  MelodicFigure out = fig;
  const int targetCount = int(std::round(float(fig.note_count()) * (1.0f + amount)));
  int safety = 0;
  const int maxIter = targetCount * 3 + 4;
  while (out.note_count() < targetCount && safety++ < maxIter) {
    int addAt = rng.int_range(0, out.note_count() - 1);
    float pick = rng.value();
    try {
      if      (pick < 0.4f) out = split(out, addAt, 2);
      else if (pick < 0.8f) out = add_neighbor(out, addAt, rng.decide(0.5f));
      else                  out = add_turn    (out, addAt, rng.decide(0.5f));
    } catch (const std::invalid_argument&) {
      continue; // addAt was invalid for that transform; try again
    }
  }
  return out;
}
```

- [ ] **Step 5: Add `embellish`.**

```cpp
// embellish(fig, rng, count): attach a default Ornament (currently just
// marking via articulation) to `count` randomly-chosen units. Unit count
// and length unchanged. Round-1 placeholder: flips articulation to
// Accent; later rounds populate with varied Ornament types.
inline MelodicFigure embellish(const MelodicFigure& fig, Randomizer& rng,
                               int count) {
  MelodicFigure out = fig;
  const int n = out.note_count();
  if (n == 0 || count <= 0) return out;
  count = std::min(count, n);
  // Sample without replacement by shuffling indices.
  std::vector<int> idx(n);
  for (int i = 0; i < n; ++i) idx[i] = i;
  for (int i = n - 1; i > 0; --i) {
    int j = rng.int_range(0, i);
    std::swap(idx[i], idx[j]);
  }
  for (int k = 0; k < count; ++k) {
    out.units[idx[k]].articulation = articulations::Accent{};
  }
  return out;
}
```

Note: `embellish` is intentionally a round-1 placeholder that only flips articulations. A richer version that varies across Ornament types is a future pass.

- [ ] **Step 6: Build + commit.**

```bash
cmake --build build --config Debug --target mforce_engine
git add engine/include/mforce/music/figure_transforms.h
git commit -m "feat(figures): figure_transforms randomized set — vary+vary_rhythm+vary_steps+complexify+embellish

Spec gap: spec had only unified vary(); legacy call sites (templates.h
TransformOp cases) use vary_rhythm and vary_steps as distinct operations.
Added all three to preserve migration fidelity. Consolidated vary uses
the other two as atoms. complexify composes split/add_neighbor/add_turn."
```

---

## Phase 4 — RandomFigureBuilder

### Task 9: Create RFB class skeleton

**Files:**
- Create: `engine/include/mforce/music/random_figure_builder.h`

- [ ] **Step 1: Write the skeleton.**

```cpp
#pragma once
#include "mforce/music/figures.h"
#include "mforce/music/figure_constraints.h"
#include "mforce/music/shape_figures.h"
#include "mforce/music/figure_transforms.h"
#include "mforce/core/randomizer.h"
#include <stdexcept>
#include <algorithm>

namespace mforce {

class RandomFigureBuilder {
public:
  explicit RandomFigureBuilder(uint32_t seed)
      : pulseGen(seed), stepGen(seed + 1) {}

  // Authoritative entry point. Satisfy every set constraint or throw.
  MelodicFigure build(const Constraints& c);

  // Convenience methods. Each merges its primary arg into a local
  // Constraints and delegates to build(c). Throws if caller's c already
  // has the field that would be set (presence-level strictness per spec).
  MelodicFigure build_by_count  (int count,               const Constraints& c = {});
  MelodicFigure build_by_length (float length,            const Constraints& c = {});
  MelodicFigure build_by_steps  (const StepSequence& ss,  const Constraints& c = {});
  MelodicFigure build_by_rhythm (const PulseSequence& ps, const Constraints& c = {});
  MelodicFigure build_singleton (const Constraints& c = {});

private:
  PulseGenerator pulseGen;
  StepGenerator  stepGen;

  // Helpers declared later; definitions in-line below.
  int  resolve_count_(const Constraints& c);
  bool feasible_(const Constraints& c, int count) const;
  MelodicFigure wander_(int count, const Constraints& c);
  MelodicFigure post_clamp_(MelodicFigure fig, const Constraints& c);
  bool satisfies_(const MelodicFigure& fig, const Constraints& c) const;
};

} // namespace mforce
```

Method bodies will be added in subsequent tasks.

- [ ] **Step 2: Add bare stub bodies so the file compiles (implementations added in next tasks).**

Append to the file, still inside `namespace mforce`:

```cpp
inline int RandomFigureBuilder::resolve_count_(const Constraints& c) {
  if (c.count) return *c.count;
  if (c.length && c.defaultPulse) {
    int n = int(std::round(*c.length / *c.defaultPulse));
    return std::max(1, n);
  }
  // Default range 4..8
  static Randomizer countRng(0x0C117u);
  return countRng.int_range(4, 8);
}

inline bool RandomFigureBuilder::feasible_(const Constraints& c, int count) const {
  if (count < 1) return false;
  if (c.minPulse && c.length && (*c.minPulse) * float(count) > *c.length) return false;
  return true;
}

inline bool RandomFigureBuilder::satisfies_(const MelodicFigure& fig, const Constraints& c) const {
  if (c.net && fig.net_step() != *c.net) return false;
  if (c.ceiling || c.floor) {
    int pos = 0, hi = 0, lo = 0;
    for (const auto& u : fig.units) { pos += u.step; hi = std::max(hi, pos); lo = std::min(lo, pos); }
    if (c.ceiling && hi > *c.ceiling) return false;
    if (c.floor   && lo < *c.floor)   return false;
  }
  return true;
}

inline MelodicFigure RandomFigureBuilder::wander_(int count, const Constraints& c) {
  float pulse = c.defaultPulse.value_or(1.0f);
  PulseSequence ps = c.length
      ? pulseGen.generate(*c.length, pulse)
      : pulseGen.generate_count(count, pulse);

  // StepGenerator emits count steps; we prepend a 0 and keep count-1 from it
  // to honor units[0].step=0.
  StepSequence raw = (c.net
      ? stepGen.targeted_sequence(count - 1, *c.net)
      : stepGen.random_sequence(count - 1));
  StepSequence ss;
  ss.add(0);
  for (int i = 0; i < raw.count(); ++i) ss.add(raw.get(i));

  // Truncate/grow ps to match ss count if generate() returned a different size.
  while (int(ps.pulses.size()) < ss.count()) ps.add(pulse);
  ps.pulses.resize(ss.count());

  return MelodicFigure::from_steps(ss, pulse);  // Note: from_steps uses uniform pulse
  // TODO: replace from_steps with a ps+ss constructor so we keep the generated rhythm.
}

inline MelodicFigure RandomFigureBuilder::post_clamp_(MelodicFigure fig, const Constraints& c) {
  // No-op round 1 — clamping lives in wander_ via StepGenerator targeting.
  // Left as a seam for later rounds.
  return fig;
}

// Convenience methods: delegate with strictness check.
inline MelodicFigure RandomFigureBuilder::build_by_count(int count, const Constraints& c) {
  if (c.count) throw std::invalid_argument("build_by_count: c.count already set");
  Constraints merged = c; merged.count = count;
  return build(merged);
}

inline MelodicFigure RandomFigureBuilder::build_by_length(float length, const Constraints& c) {
  if (c.length) throw std::invalid_argument("build_by_length: c.length already set");
  Constraints merged = c; merged.length = length;
  return build(merged);
}

inline MelodicFigure RandomFigureBuilder::build_by_steps(const StepSequence& ss, const Constraints& c) {
  if (c.count) throw std::invalid_argument("build_by_steps: c.count already set");
  if (c.net)   throw std::invalid_argument("build_by_steps: c.net already set");
  Constraints merged = c;
  merged.count = ss.count();
  int net = 0; for (int i = 0; i < ss.count(); ++i) net += ss.get(i);
  merged.net = net;
  // Produce rhythm only; assemble with caller's ss.
  float pulse = c.defaultPulse.value_or(1.0f);
  PulseSequence ps = c.length
      ? pulseGen.generate(*c.length, pulse)
      : pulseGen.generate_count(ss.count(), pulse);
  while (int(ps.pulses.size()) < ss.count()) ps.add(pulse);
  ps.pulses.resize(ss.count());
  MelodicFigure fig(ps, ss);
  if (!fig.units.empty()) fig.units[0].step = 0; // honor convention
  return fig;
}

inline MelodicFigure RandomFigureBuilder::build_by_rhythm(const PulseSequence& ps, const Constraints& c) {
  if (c.count)  throw std::invalid_argument("build_by_rhythm: c.count already set");
  if (c.length) throw std::invalid_argument("build_by_rhythm: c.length already set");
  Constraints merged = c;
  merged.count = ps.count();
  float total = 0; for (auto d : ps.pulses) total += d;
  merged.length = total;
  // Produce steps only; caller's ps drives rhythm.
  StepSequence raw = (c.net
      ? stepGen.targeted_sequence(ps.count() - 1, *c.net)
      : stepGen.random_sequence(ps.count() - 1));
  StepSequence ss; ss.add(0);
  for (int i = 0; i < raw.count(); ++i) ss.add(raw.get(i));
  return MelodicFigure(ps, ss);
}

inline MelodicFigure RandomFigureBuilder::build_singleton(const Constraints& c) {
  if (c.count) throw std::invalid_argument("build_singleton: c.count already set");
  float dur = c.length.value_or(c.defaultPulse.value_or(1.0f));
  MelodicFigure fig;
  fig.units.push_back({dur, 0});
  return fig;
}

// build(c) — round-1 implementation: feasibility filter + weighted coin flip
// with 3-retry. See Task 10 for full body.
inline MelodicFigure RandomFigureBuilder::build(const Constraints& c) {
  int count = resolve_count_(c);
  if (!feasible_(c, count))
    throw std::invalid_argument("RandomFigureBuilder::build: infeasible constraints");

  for (int attempt = 0; attempt < 3; ++attempt) {
    MelodicFigure fig = wander_(count, c);
    fig = post_clamp_(fig, c);
    if (satisfies_(fig, c)) return fig;
  }
  throw std::runtime_error("RandomFigureBuilder::build: failed after 3 retries");
}

} // namespace mforce
```

Note this initial body uses only the `wander_` path (no shape selection yet). Task 10 adds the weighted coin flip.

- [ ] **Step 3: Build.**

```bash
cmake --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/random_figure_builder.h
git commit -m "feat(figures): RandomFigureBuilder class with convenience methods + wander fallback

Round 1 build(c) uses only the wander path; Task 10 adds the weighted
coin flip over shape_figures."
```

---

### Task 10: Add weighted coin-flip shape selection to build(c)

**Files:**
- Modify: `engine/include/mforce/music/random_figure_builder.h`

- [ ] **Step 1: Add `try_*` helpers and weighted dispatcher.**

Replace the body of `RandomFigureBuilder::build` with:

```cpp
inline MelodicFigure RandomFigureBuilder::build(const Constraints& c) {
  int count = resolve_count_(c);
  if (!feasible_(c, count))
    throw std::invalid_argument("RandomFigureBuilder::build: infeasible constraints");

  for (int attempt = 0; attempt < 3; ++attempt) {
    MelodicFigure fig;
    float pulse = c.defaultPulse.value_or(1.0f);

    // Filter feasible shapes by count
    bool canRun       = count >= 2;
    bool canRepeats   = count >= 1;
    bool canNeighbor  = count >= 3;
    bool canLeapFill  = count >= 2;
    bool canArc       = count >= 3;
    bool canZigzag    = count >= 3;

    // Weighted random selection (fallthrough to wander)
    float r = pulseGen.rng.value(); // reuse pulseGen's rng as the selector
    float acc = 0.0f;
    auto eat = [&](bool available, float w) {
      if (!available) return false;
      acc += w; return r < acc;
    };

    if      (eat(canArc,      0.30f)) fig = try_arc_         (count, c, pulse);
    else if (eat(canRun,      0.20f)) fig = try_run_         (count, c, pulse);
    else if (eat(canZigzag,   0.10f)) fig = try_zigzag_      (count, c, pulse);
    else if (eat(canNeighbor, 0.10f)) fig = try_neighbor_    (count, c, pulse);
    else if (eat(canLeapFill, 0.10f)) fig = try_leap_fill_   (count, c, pulse);
    else                              fig = wander_(count, c);

    fig = post_clamp_(fig, c);
    if (satisfies_(fig, c)) return fig;
  }
  throw std::runtime_error("RandomFigureBuilder::build: failed after 3 retries");
}
```

- [ ] **Step 2: Add the `try_*` helpers.**

Before `RandomFigureBuilder::build(...)`, add the private helper methods. Also declare them in the class definition (add to the private section):

```cpp
// (inside class private section, declarations)
MelodicFigure try_arc_      (int count, const Constraints& c, float pulse);
MelodicFigure try_run_      (int count, const Constraints& c, float pulse);
MelodicFigure try_zigzag_   (int count, const Constraints& c, float pulse);
MelodicFigure try_neighbor_ (int count, const Constraints& c, float pulse);
MelodicFigure try_leap_fill_(int count, const Constraints& c, float pulse);
```

And the inline definitions, placed after the existing helper bodies:

```cpp
inline MelodicFigure RandomFigureBuilder::try_arc_(int count, const Constraints& c, float pulse) {
  // Shape: 1 + extent + returnExtent = count. Split halves (or per c.net).
  int total = count - 1;
  int extent, returnExtent;
  if (c.net) {
    int n = *c.net;
    // |net| = extent - returnExtent; extent + returnExtent = total
    int absN = std::abs(n);
    if (absN > total) throw std::runtime_error("try_arc_: net too large for count");
    extent = (total + absN) / 2;
    returnExtent = total - extent;
    if (n < 0) std::swap(extent, returnExtent);
  } else {
    extent = total / 2 + (total % 2);
    returnExtent = total - extent;
  }
  int direction = (c.net.value_or(+1) >= 0) ? +1 : -1;
  return shape_figures::arc(direction, extent, returnExtent, pulse);
}

inline MelodicFigure RandomFigureBuilder::try_run_(int count, const Constraints& c, float pulse) {
  int direction;
  if (c.net) direction = (*c.net >= 0) ? +1 : -1;
  else direction = (stepGen.rng.int_range(0, 1) == 0) ? +1 : -1;
  // Note: a run of count units has net = ±(count-1); caller's c.net may conflict.
  return shape_figures::run(direction, count, pulse);
}

inline MelodicFigure RandomFigureBuilder::try_zigzag_(int count, const Constraints& c, float pulse) {
  // count = 1 + 2*cycles → cycles = (count-1)/2. Round down; one trailing unit may be off-grid.
  int cycles = (count - 1) / 2;
  if (cycles < 1) cycles = 1;
  int direction = (c.net.value_or(+1) >= 0) ? +1 : -1;
  return shape_figures::zigzag(direction, cycles, 2, 1, pulse);
}

inline MelodicFigure RandomFigureBuilder::try_neighbor_(int count, const Constraints& c, float pulse) {
  // neighbor() is always 3 units; if count > 3, combine with a run for the tail.
  MelodicFigure base = shape_figures::neighbor(stepGen.rng.decide(0.5f), pulse);
  if (count == 3) return base;
  // Append a run of (count - 3) tail units at step=+1 or -1 (matches direction of c.net if set)
  int direction = (c.net.value_or(+1) >= 0) ? +1 : -1;
  MelodicFigure tail = shape_figures::run(direction, count - 2, pulse); // count-2 because combine drops the tail's first unit
  // tail.units[0].step is 0; connector leadStep stays 0 (inherit running pitch)
  return figure_transforms::combine(base, tail, 0, false);
}

inline MelodicFigure RandomFigureBuilder::try_leap_fill_(int count, const Constraints& c, float pulse) {
  // leap_and_fill emits 1 + 1 + fillSteps = count → fillSteps = count - 2.
  int leapSize = 3; // round-1 default; tuning later
  bool leapUp = (c.net.value_or(+1) >= 0);
  int fillSteps = count - 2;
  if (fillSteps < 0) fillSteps = 0;
  return shape_figures::leap_and_fill(leapSize, leapUp, fillSteps, pulse);
}
```

- [ ] **Step 3: Build.**

```bash
cmake --build build --config Debug --target mforce_engine
```
Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/random_figure_builder.h
git commit -m "feat(figures): RFB build(c) with weighted coin flip over shape_figures

Round-1 weights: arc 0.30 / run 0.20 / zigzag 0.10 / neighbor-pair 0.10 /
leap_and_fill 0.10 / wander (fallthrough) 0.20. 3-retry-then-throw
discipline on constraint violation."
```

---

## Phase 5 — Migration

### Task 11: Migrate templates.h TransformOp cases

**Files:**
- Modify: `engine/include/mforce/music/templates.h:560-614` (TransformOp switch)

- [ ] **Step 1: Add include.**

Near the top of `templates.h`, add:
```cpp
#include "mforce/music/figure_transforms.h"
```

- [ ] **Step 2: Replace the Invert case (lines 563-570).**

```cpp
case TransformOp::Invert: {
    if (!std::holds_alternative<MelodicFigure>(parent->content))
        throw std::runtime_error("Invert requires MelodicFigure parent");
    derived.content = figure_transforms::invert(std::get<MelodicFigure>(parent->content));
    break;
}
```

- [ ] **Step 3: Replace the Reverse case (lines 571-577). Note the op name stays `Reverse` in the enum but the transform is now `retrograde_steps`.**

```cpp
case TransformOp::Reverse: {
    if (!std::holds_alternative<MelodicFigure>(parent->content))
        throw std::runtime_error("Reverse requires MelodicFigure parent");
    derived.content = figure_transforms::retrograde_steps(std::get<MelodicFigure>(parent->content));
    break;
}
```

- [ ] **Step 4: Replace the VarySteps case (lines 578-586).**

```cpp
case TransformOp::VarySteps: {
    if (!std::holds_alternative<MelodicFigure>(parent->content))
        throw std::runtime_error("VarySteps requires MelodicFigure parent");
    Randomizer rng(parent->generationSeed + 2);
    MelodicFigure copy = std::get<MelodicFigure>(parent->content);
    int variations = transformParam > 0 ? transformParam : 1;
    derived.content = figure_transforms::vary_steps(copy, rng, variations);
    break;
}
```

- [ ] **Step 5: Replace the VaryRhythm case (lines 587-593).**

```cpp
case TransformOp::VaryRhythm: {
    if (!std::holds_alternative<MelodicFigure>(parent->content))
        throw std::runtime_error("VaryRhythm requires MelodicFigure parent");
    Randomizer rng(parent->generationSeed + 3);
    derived.content = figure_transforms::vary_rhythm(std::get<MelodicFigure>(parent->content), rng);
    break;
}
```

- [ ] **Step 6: Check for Stretch / Compress cases (may exist later in the switch).**

Search for `case TransformOp::Stretch`, `case TransformOp::Compress` in the file. If present, update them to use `figure_transforms::` — but there aren't deterministic `stretch`/`compress` in the new API. These weren't ported to the new `figure_transforms::` spec.

**Spec gap:** add `stretch` and `compress` to `figure_transforms::` now. Open `figure_transforms.h` and add:

```cpp
// stretch(fig, factor): scale all durations by factor. factor>1 lengthens.
inline MelodicFigure stretch(const MelodicFigure& fig, float factor) {
  MelodicFigure out = fig;
  for (auto& u : out.units) u.duration *= factor;
  return out;
}

// compress(fig, factor): scale all durations by 1/factor. factor>1 shortens.
inline MelodicFigure compress(const MelodicFigure& fig, float factor) {
  if (factor <= 0.0f) throw std::invalid_argument("compress: factor must be > 0");
  MelodicFigure out = fig;
  for (auto& u : out.units) u.duration /= factor;
  return out;
}
```

Place them next to `set_last_pulse` / `adjust_last_pulse` in the deterministic section.

Then the Stretch/Compress cases in templates.h map to `figure_transforms::stretch(...)` / `compress(...)`.

- [ ] **Step 7: Remove the now-unused `FigureBuilder fb(...)` lines in this switch** (they're orphaned after the above replacements).

- [ ] **Step 8: Build.**

```bash
cmake --build build --config Debug --target mforce_engine
cmake --build build --config Debug --target mforce_cli
```
Expected: clean build. Any remaining `FigureBuilder` references in templates.h should be gone.

- [ ] **Step 9: Commit.**

```bash
git add engine/include/mforce/music/figure_transforms.h engine/include/mforce/music/templates.h
git commit -m "refactor(templates): migrate TransformOp cases to figure_transforms namespace

Also: add stretch/compress to figure_transforms (spec gap — legacy
TransformOp variants needed them)."
```

---

### Task 12: Migrate default_strategies.h

**Files:**
- Modify: `engine/include/mforce/music/default_strategies.h:55-145`

- [ ] **Step 1: Add includes.**

Near the top of the file:
```cpp
#include "mforce/music/random_figure_builder.h"
#include "mforce/music/figure_transforms.h"
```

- [ ] **Step 2: Rewrite `DefaultFigureStrategy::generate_figure` (lines 55-117).**

Replace the whole body with:

```cpp
inline MelodicFigure DefaultFigureStrategy::generate_figure(
    const FigureTemplate& figTmpl, uint32_t seed) {
  StepGenerator sg(seed);

  float defaultPulse = (figTmpl.defaultPulse > 0) ? figTmpl.defaultPulse : 1.0f;

  // Step generation path — preserves legacy StepGenerator mode selection.
  auto generate_steps = [&](int noteCount) -> StepSequence {
    if (figTmpl.targetNet != 0) {
      return sg.targeted_sequence(noteCount, figTmpl.targetNet);
    } else if (figTmpl.preferStepwise) {
      return sg.no_skip_sequence(noteCount);
    } else {
      float skipProb = figTmpl.preferSkips ? 0.6f : 0.3f;
      return sg.random_sequence(noteCount, skipProb);
    }
  };

  auto clamp_steps = [&](StepSequence& ss) {
    if (figTmpl.maxStep > 0) {
      for (int i = 0; i < ss.count(); ++i) {
        if (ss.steps[i] > figTmpl.maxStep) ss.steps[i] = figTmpl.maxStep;
        else if (ss.steps[i] < -figTmpl.maxStep) ss.steps[i] = -figTmpl.maxStep;
      }
    }
  };

  if (figTmpl.totalBeats > 0) {
    int noteCount = int(figTmpl.totalBeats / defaultPulse);
    noteCount = std::clamp(noteCount, figTmpl.minNotes, figTmpl.maxNotes);

    StepSequence ss = generate_steps(noteCount);
    clamp_steps(ss);

    MelodicFigure fig = MelodicFigure::from_steps(ss, defaultPulse);
    if (!fig.units.empty()) fig.units[0].step = 0;  // honor convention

    Randomizer varyRng(seed + 2);
    if (varyRng.decide(0.4f)) {
      fig = figure_transforms::vary_rhythm(fig, varyRng);
    }
    return fig;
  }

  Randomizer countRng(seed + 3);
  int noteCount = countRng.int_range(figTmpl.minNotes, figTmpl.maxNotes);

  StepSequence ss = generate_steps(noteCount);
  clamp_steps(ss);

  MelodicFigure fig = MelodicFigure::from_steps(ss, defaultPulse);
  if (!fig.units.empty()) fig.units[0].step = 0;
  return fig;
}
```

- [ ] **Step 3: Rewrite `DefaultFigureStrategy::apply_transform` (lines 119-145).**

Replace with:

```cpp
inline MelodicFigure DefaultFigureStrategy::apply_transform(
    const MelodicFigure& base, TransformOp op, int param, uint32_t seed) {
  Randomizer rng(seed);

  switch (op) {
    case TransformOp::Invert:   return figure_transforms::invert(base);
    case TransformOp::Reverse:  return figure_transforms::retrograde_steps(base);
    case TransformOp::Stretch:  return figure_transforms::stretch(base, param > 0 ? float(param) : 2.0f);
    case TransformOp::Compress: return figure_transforms::compress(base, param > 0 ? float(param) : 2.0f);
    // ... preserve any other cases present in the original body verbatim,
    // replacing fb.X(...) with figure_transforms::X(...).
  }
  return base; // fallback — shouldn't hit, keep legacy behavior
}
```

Scan the original `apply_transform` body (lines 119-145 or as extended) for any case you haven't handled. Port each one.

- [ ] **Step 4: Build.**

```bash
cmake --build build --config Debug --target mforce_engine
cmake --build build --config Debug --target mforce_cli
```
Expected: clean build. No `FigureBuilder` references left in this file.

- [ ] **Step 5: Commit.**

```bash
git add engine/include/mforce/music/default_strategies.h
git commit -m "refactor(strategies): migrate DefaultFigureStrategy off FigureBuilder

Uses MelodicFigure::from_steps + figure_transforms::* for generation
and transforms. StepGenerator mode-selection logic preserved verbatim."
```

---

### Task 13: Migrate composer.h fallback

**Files:**
- Modify: `engine/include/mforce/music/composer.h:982`

- [ ] **Step 1: Inspect the site.**

```bash
grep -n "FigureBuilder" engine/include/mforce/music/composer.h
```
Expected: one hit around line 982: `return FigureBuilder(::mforce::rng::next()).single_note(1.0f);`

- [ ] **Step 2: Replace with the new API.**

A single-pulse figure of duration 1.0 is a `build_singleton` with `length=1.0f`. Replace the line with:

```cpp
      RandomFigureBuilder rfb(::mforce::rng::next());
      Constraints c; c.length = 1.0f;
      return rfb.build_singleton(c);
```

Add `#include "mforce/music/random_figure_builder.h"` near the top of `composer.h` if not already present.

- [ ] **Step 3: Build.**

```bash
cmake --build build --config Debug --target mforce_engine
cmake --build build --config Debug --target mforce_cli
```
Expected: clean build.

- [ ] **Step 4: Commit.**

```bash
git add engine/include/mforce/music/composer.h
git commit -m "refactor(composer): migrate fallback single-pulse figure to RandomFigureBuilder"
```

---

### Task 14: Delete shape_strategies.h + remove registrations

**Files:**
- Delete: `engine/include/mforce/music/shape_strategies.h`
- Modify: any `.cpp` or registration site that registers those strategies (search required).

- [ ] **Step 1: Find registration sites.**

```bash
grep -rn "ShapeScalarRunStrategy\|ShapeRepeatedNoteStrategy\|ShapeHeldNoteStrategy\|ShapeCadentialApproachStrategy\|ShapeTriadicOutlineStrategy\|ShapeNeighborToneStrategy\|ShapeLeapAndFillStrategy\|ShapeScalarReturnStrategy\|ShapeAnacrusisStrategy\|ShapeZigzagStrategy\|ShapeFanfareStrategy\|ShapeSighStrategy\|ShapeSuspensionStrategy\|ShapeCambiataStrategy\|ShapeSkippingStrategy\|ShapeSteppingStrategy" engine/ tools/
```

These are likely registered in a file such as `engine/src/source_registrations.cpp` or elsewhere via `StrategyRegistry`.

- [ ] **Step 2: Remove `#include "mforce/music/shape_strategies.h"` and the registration calls** at each site found. If a registration is of a shape strategy that is still needed (e.g. someone references "shape_run" by name via JSON), that's a **spec gap** — flag it. Expected: none, since JSON patches don't reference these shape-strategy names.

- [ ] **Step 3: Delete the file.**

```bash
git rm engine/include/mforce/music/shape_strategies.h
```

- [ ] **Step 4: Build both engine and cli.**

```bash
cmake --build build --config Debug --target mforce_engine
cmake --build build --config Debug --target mforce_cli
```
Expected: clean build.

- [ ] **Step 5: Commit.**

```bash
git add -A
git commit -m "refactor(strategies): delete shape_strategies.h — redundant with shape_figures

16 ShapeXxxStrategy wrapper classes removed. Callers wanting a specific
shape should call shape_figures:: directly, or rely on
RandomFigureBuilder::build(c)'s weighted dispatcher."
```

---

### Task 15: Delete old FigureBuilder struct

**Files:**
- Modify: `engine/include/mforce/music/figures.h:722-1145` (remove `struct FigureBuilder`)

- [ ] **Step 1: Confirm no callers remain.**

```bash
grep -rn "FigureBuilder" engine/ tools/
```
Expected: zero hits (prior tasks migrated all call sites).

- [ ] **Step 2: Delete the struct.**

Open `engine/include/mforce/music/figures.h`. Locate the `// ------- FigureBuilder ...` comment and the `struct FigureBuilder { ... };` that follows (approximately lines 722-1145). Delete the entire block including its header comment.

- [ ] **Step 3: Build engine and cli.**

```bash
cmake --build build --config Debug --target mforce_engine
cmake --build build --config Debug --target mforce_cli
```
Expected: clean build. No lingering `FigureBuilder` references.

- [ ] **Step 4: Verify grep is clean.**

```bash
grep -rn "FigureBuilder" engine/ tools/
```
Expected: zero hits.

- [ ] **Step 5: Commit.**

```bash
git add engine/include/mforce/music/figures.h
git commit -m "refactor(figures): delete FigureBuilder struct — replaced by RFB + shape_figures + figure_transforms

End of the round-1 redesign. The monolith is gone; its responsibilities
are now cleanly split across RandomFigureBuilder (stochastic generation),
shape_figures:: (pure shape templates), and figure_transforms:: (operators
on existing figures)."
```

---

## Phase 6 — Validation

### Task 16: Re-baseline K467 renders

**Files:**
- Regenerate: `renders/test_k467_period_*.wav`, `renders/test_k467_period_*.json`, and any other K467 patches used as goldens.

- [ ] **Step 1: Identify baseline patches.**

```bash
ls patches/test_k467_*.json
```
Expected candidates: `test_k467_period.json`, `test_k467_walker.json`, `test_k467_harmony.json`, others as present.

- [ ] **Step 2: Render each.**

For each patch, run (adjust file names / bpm as the existing baselines used):

```bash
build/x64/Debug/mforce_cli.exe --compose patches/test_k467_period.json  renders/test_k467_period.wav  100 --save-rendered renders/
build/x64/Debug/mforce_cli.exe --compose patches/test_k467_walker.json  renders/test_k467_walker.wav  100 --save-rendered renders/
build/x64/Debug/mforce_cli.exe --compose patches/test_k467_harmony.json renders/test_k467_harmony.wav 100 --save-rendered renders/
```

Expected: each command exits 0 and writes `renders/<name>_1.{wav,json}` (plus `_2`, `_3` for multi-Part pieces).

- [ ] **Step 3: Spot-check renders.**

Listen to each WAV. Verify:
- Audio plays without clipping, silence, or gross artifacts.
- Non-literal phrases (pure-Generate paths) sound "like music" at a basic level (no obvious stuck-notes or silent bars).

Matt's prior bar: K467 literal motif-reference phrases should remain musically faithful; pure-Generate phrases will sound different (and hopefully, per this redesign's goal, somewhat better-shaped due to the arc/neighbor bias in the new coin-flip dispatcher).

- [ ] **Step 4: Review rendered JSON diffs.**

For each `renders/*.json` that existed in main pre-redesign, compare structurally (unified diff is noisy but informative):

```bash
git diff --stat renders/test_k467_period_*.json
```

Expected: substantial diffs (this is the spec's acknowledged drift). Look for:
- NO unexpected zero-duration units.
- NO step values outside reasonable range (e.g., |step| > 10 without a chord-tone walker).
- Phrase structure (phrase count, figure counts) broadly preserved.

- [ ] **Step 5: Commit the regenerated goldens.**

```bash
git add renders/test_k467_*.wav renders/test_k467_*.json
git commit -m "chore(renders): re-baseline K467 goldens after FigureBuilder redesign

Drift expected per spec — PulseGenerator now drives rhythm for stochastic
figures (richer alphabet than old inline uniform), and seed lineage of
two generators differs from old single-rng. Literal motif-reference paths
should remain musically faithful; pure-Generate paths audibly different."
```

---

## Self-review notes

Coverage check against spec:
- ✔️ Bible rules — embedded in Tasks 1, 4 (step[0]=0 in shapes), 9 (step[0]=0 in RFB), 12 (honoring convention in DefaultFigureStrategy).
- ✔️ Structural split — Tasks 4, 5, 9 create the three components in separate namespaces/classes.
- ✔️ Generator hierarchy — Task 9 makes RFB own `PulseGenerator` + `StepGenerator`; Task 2 extends PulseGenerator.
- ✔️ `Constraints` struct — Task 1.
- ✔️ RFB method set — Task 9 + 10.
- ✔️ `build(c)` algorithm — Task 10.
- ✔️ Convenience-method strictness — Task 9 (in each convenience method's delegate).
- ✔️ `FigureTransforms` method set — Tasks 5 (invert/retrograde), 6 (combine/replicate/prune/set+adjust_last_pulse), 7 (specialty + micro-inserts), 8 (randomized).
- ✔️ `ShapeFigures` method set — Task 4.
- ✔️ Migration plan — Tasks 11-15.
- ✔️ Re-baseline — Task 16.

Spec gaps addressed inline:
- `MelodicFigure::from_steps` helper added (Task 3) — fills the gap left by `FigureBuilder::build(ss, pulse)` having no new-API equivalent.
- `vary_rhythm` / `vary_steps` kept as distinct transforms alongside consolidated `vary` (Task 8) — preserves migration fidelity for `TransformOp::VarySteps`/`VaryRhythm`.
- `stretch` / `compress` added to `figure_transforms::` (Task 11) — required for `TransformOp::Stretch`/`Compress` migration.

Parked (not in this plan, for future rounds):
- `note` → `pulse` / `unit` rename sweep across the whole codebase.
- `pitch_reader.h` vs `pitch_walker.h` consolidation.
- `runningReader` naming revisit (at Phrase/Passage layer, not here).
- Shape-weight tuning via `Constraints` or `GenerationPreferences`.
- Rich `embellish` (round-1 placeholder; real Ornament-variety version later).
- Phrase-level redesign (the real musicality problem, per the spec's meta-scope note).
