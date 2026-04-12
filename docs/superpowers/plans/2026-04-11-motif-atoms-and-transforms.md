# Motif Atoms and Transforms Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make PulseSequence and StepSequence first-class motif types so rhythm and contour can be shared across figures as reusable raw material, enabling motivic coherence in algorithmically composed passages.

**Architecture:** Expand the existing `Motif` struct to use `std::variant<MelodicFigure, PulseSequence, StepSequence>`. Add atom-level transform methods on PulseSequence (retrograde, stretch, compress) and StepSequence (invert, retrograde, expand, contract). Add a `PulseGenerator` class parallel to the existing `StepGenerator`. FigureTemplate gains `rhythmMotifName`/`contourMotifName` fields so strategies receive motif atoms as optional inputs. The existing `MelodicFigure(PulseSequence, StepSequence)` constructor is the combiner.

**Tech Stack:** C++17, nlohmann::json, header-only. No new dependencies. No test framework — mechanical hash verification for golden preservation, WAV renders for musical validation.

---

## Scope Guardrails

- **Golden hash `8f7fc479...` must be preserved** through tasks 1–6. The golden template uses no atom motifs, no rhythmMotifName/contourMotifName, no atom transforms. Any drift means a code change leaked into the default path.
- **Existing MelodicFigure motif path is unchanged.** `Composer::realize_motifs_` continues to use the Phase 1a bypass (`DefaultFigureStrategy::generate_figure` directly) for MelodicFigure motifs. Routing them through full strategy dispatch is a future change that would re-pin the golden.
- **No `vary`, `truncate`, or `tail` transforms.** Deferred per brainstorming. Figures are atoms; author short motifs for short gestures.
- **No complex triplet scenarios** (isolated triplets tied to adjacent notes). Triplet groups are always 3 clean notes summing to a standard binary duration.
- **Strategies stay at the Figure level.** No PulseStrategy or StepStrategy classes.
- **Single branch: `main`.** Commit directly.

---

## File Structure

| File | Changes |
|---|---|
| `engine/include/mforce/music/figures.h` | Add 4 transform methods on `StepSequence`, 3 on `PulseSequence`. Add `extract_pulses()`/`extract_steps()` on `MelodicFigure`. Update `MelodicFigure(PS, SS)` constructor. Add `PulseGenerator` class. |
| `engine/include/mforce/music/templates.h` | Expand `Motif` to use `std::variant`. Add `rhythmMotifName`, `contourMotifName`, `rhythmTransform`, `rhythmTransformParam`, `contourTransform`, `contourTransformParam` to `FigureTemplate`. |
| `engine/include/mforce/music/templates_json.h` | JSON round-trip for variant `Motif`, new FigureTemplate fields, atom transforms. |
| `engine/include/mforce/music/music_json.h` | Standalone `to_json`/`from_json` for `PulseSequence` and `StepSequence` (currently only serialized as part of MelodicFigure). |
| `engine/include/mforce/music/composer.h` | `realize_motifs_` handles atom motifs via PulseGenerator/StepGenerator. Add `find_rhythm_motif()`/`find_contour_motif()` accessors. |
| `engine/include/mforce/music/shape_strategies.h` | Update Skipping/Stepping/CadentialApproach to check for motif inputs. |
| `engine/include/mforce/music/rhythm_util.h` | Absorb into PulseGenerator in `figures.h`; file may be deleted or kept as a thin redirect. |
| `patches/test_k467_motifs.json` | New template: K467 opening with atom motifs for coherence. |

---

## Task 1: Atom transforms on StepSequence and PulseSequence

**Files:**
- Modify: `engine/include/mforce/music/figures.h`

Add transform methods directly on the atom structs. All transforms preserve element count. No external dependencies — pure value transforms.

- [ ] **Step 1: Add StepSequence transforms**

In `engine/include/mforce/music/figures.h`, find `struct StepSequence` (around line 32). Add 4 methods after the existing `net()` method:

```cpp
struct StepSequence {
  std::vector<int> steps;

  void add(int step) { steps.push_back(step); }
  int count() const { return int(steps.size()); }
  int get(int i) const { return steps[i]; }

  int peak() const { int p = 0, c = 0; for (int s : steps) { c += s; p = std::max(p, c); } return p; }
  int floor() const { int f = 0, c = 0; for (int s : steps) { c += s; f = std::min(f, c); } return f; }
  int range() const { return peak() - floor(); }
  int net() const { int c = 0; for (int s : steps) c += s; return c; }

  // --- Transforms (all return new StepSequence, source unchanged) ---

  StepSequence inverted() const {
    StepSequence out;
    out.steps.reserve(steps.size());
    for (int s : steps) out.steps.push_back(-s);
    return out;
  }

  StepSequence retrograded() const {
    StepSequence out;
    out.steps.assign(steps.rbegin(), steps.rend());
    return out;
  }

  StepSequence expanded(float factor) const {
    StepSequence out;
    out.steps.reserve(steps.size());
    for (int s : steps) out.steps.push_back(int(std::round(s * factor)));
    return out;
  }

  StepSequence contracted(float factor) const {
    if (factor <= 0) return *this;
    return expanded(1.0f / factor);
  }
};
```

- [ ] **Step 2: Add PulseSequence transforms**

In the same file, find `struct PulseSequence` (around line 15). Add 3 methods after the existing `total_length()` method:

```cpp
struct PulseSequence {
  std::vector<float> pulses;

  void add(float beats) { pulses.push_back(beats); }
  int count() const { return int(pulses.size()); }
  float get(int i) const { return pulses[i]; }

  float total_length() const {
    float t = 0;
    for (float p : pulses) t += p;
    return t;
  }

  // --- Transforms (all return new PulseSequence, source unchanged) ---

  PulseSequence retrograded() const {
    PulseSequence out;
    out.pulses.assign(pulses.rbegin(), pulses.rend());
    return out;
  }

  PulseSequence stretched(float factor) const {
    PulseSequence out;
    out.pulses.reserve(pulses.size());
    for (float p : pulses) out.pulses.push_back(p * factor);
    return out;
  }

  PulseSequence compressed(float factor) const {
    if (factor <= 0) return *this;
    return stretched(1.0f / factor);
  }
};
```

- [ ] **Step 3: Build and verify golden**

```
"/c/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --target mforce_cli --config Release
```

Then render golden and verify hash:
```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/t1_check 1 --template patches/template_golden_phase1a.json
sha256sum renders/t1_check_1.wav
```
Expected: `8f7fc479162c5878fca15e327506d9b0c57bafd80ebb07aff6adc0e801bfb973`

- [ ] **Step 4: Commit**

```
git add engine/include/mforce/music/figures.h
git commit -m "feat(composer): add atom-level transforms on StepSequence and PulseSequence

StepSequence: inverted(), retrograded(), expanded(factor), contracted(factor)
PulseSequence: retrograded(), stretched(factor), compressed(factor)

All transforms preserve element count and return new instances (source
unchanged). No vary/truncate/tail — deferred per spec. stretch/compress
change total duration (augmentation/diminution); expand/contract change
step magnitudes (interval widening/narrowing)."
```

---

## Task 2: MelodicFigure extract methods + constructor update

**Files:**
- Modify: `engine/include/mforce/music/figures.h`

- [ ] **Step 1: Add extract_pulses and extract_steps**

Find `struct MelodicFigure` (around line 375). Add after the existing `net_step()` method:

```cpp
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
```

- [ ] **Step 2: Add a new constructor overload for Phase 1b cursor model**

The existing constructor expects `steps.count() == pulses.count() - 1` (first note has no step). Under Phase 1b, every unit has a step (including unit 0). Add a new overload that takes equal-length sequences:

```cpp
  // Phase 1b constructor: steps[i] corresponds to pulses[i] one-to-one.
  // steps[0] is the first-unit step (typically 0 for "start at cursor").
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
```

Named `from_atoms` (static factory) to avoid ambiguity with the existing constructor. The existing `MelodicFigure(PulseSequence, StepSequence)` constructor is unchanged for backward compatibility.

- [ ] **Step 3: Build and verify golden**

Same build + hash check as Task 1. Expected: `8f7fc479...` (unchanged).

- [ ] **Step 4: Commit**

```
git add engine/include/mforce/music/figures.h
git commit -m "feat(composer): add extract_pulses/extract_steps + from_atoms factory on MelodicFigure"
```

---

## Task 3: PulseGenerator class

**Files:**
- Modify: `engine/include/mforce/music/figures.h` (add class after StepGenerator)
- Modify: `engine/include/mforce/music/rhythm_util.h` (mark as deprecated or delete)

- [ ] **Step 1: Add PulseGenerator to figures.h**

Find the end of `struct StepGenerator` in `figures.h` (after `random_sequence`'s closing brace — scan for the matching `};` of the StepGenerator struct). Add a new struct after it:

```cpp
// ---------------------------------------------------------------------------
// PulseGenerator — generates random PulseSequences with standard musical
// durations. Parallel to StepGenerator for the rhythm dimension.
// ---------------------------------------------------------------------------
struct PulseGenerator {
  Randomizer rng;

  explicit PulseGenerator(uint32_t seed = 0xPU15'0000u) : rng(seed) {}

  // Generate a PulseSequence of standard musical durations summing to
  // totalBeats, biased toward defaultPulse but with variety.
  // Includes both binary subdivisions and triplet groups.
  PulseSequence generate(float totalBeats, float defaultPulse = 1.0f) {
    PulseSequence ps;
    float remaining = totalBeats;

    // Binary durations (individual notes)
    static const float BINARY[] = {0.25f, 0.5f, 0.75f, 1.0f, 1.5f, 2.0f, 3.0f, 4.0f};
    // Triplet groups: each is {perNoteDuration, groupTotal}
    // Triplet sixteenths: 3 x 1/6 = 0.5 beats
    // Triplet eighths:    3 x 1/3 = 1.0 beat
    // Triplet quarters:   3 x 2/3 = 2.0 beats
    struct TripletGroup { float perNote; float total; };
    static const TripletGroup TRIPLETS[] = {
      {1.0f / 6.0f, 0.5f},
      {1.0f / 3.0f, 1.0f},
      {2.0f / 3.0f, 2.0f},
    };

    while (remaining > 0.001f) {
      // Collect binary candidates
      struct Candidate { float duration; int noteCount; bool isTriplet; float perNote; };
      std::vector<Candidate> candidates;

      for (float d : BINARY) {
        if (d <= remaining + 0.001f)
          candidates.push_back({d, 1, false, d});
      }

      // Collect triplet group candidates
      for (const auto& tg : TRIPLETS) {
        if (tg.total <= remaining + 0.001f)
          candidates.push_back({tg.total, 3, true, tg.perNote});
      }

      if (candidates.empty()) break;

      // Weight toward defaultPulse
      std::vector<float> weights;
      float weightSum = 0;
      for (auto& c : candidates) {
        // For triplets, compare the group total (not per-note) against defaultPulse
        float compareVal = c.isTriplet ? c.duration : c.duration;
        float w = 1.0f / (1.0f + std::abs(compareVal - defaultPulse) * 2.0f);
        // Slight penalty for triplets to keep them occasional, not dominant
        if (c.isTriplet) w *= 0.3f;
        weights.push_back(w);
        weightSum += w;
      }

      // Weighted random selection
      float pick = rng.value() * weightSum;
      float accum = 0;
      int idx = 0;
      for (int i = 0; i < int(weights.size()); ++i) {
        accum += weights[i];
        if (accum >= pick) { idx = i; break; }
      }

      auto& chosen = candidates[idx];
      if (chosen.isTriplet) {
        // Emit 3 notes
        for (int t = 0; t < 3; ++t) ps.add(chosen.perNote);
      } else {
        ps.add(chosen.duration);
      }
      remaining -= chosen.duration;
    }

    return ps;
  }
};
```

- [ ] **Step 2: Update shape_strategies.h to use PulseGenerator instead of generate_musical_rhythm**

Read `engine/include/mforce/music/shape_strategies.h` and find where `ShapeSkippingStrategy`, `ShapeSteppingStrategy`, and `ShapeCadentialApproachStrategy` call `generate_musical_rhythm`. Replace each call with `PulseGenerator(seed).generate(totalBeats, pulse)`. The `generate_musical_rhythm` function in `rhythm_util.h` can be left as-is (the strategies just stop using it) or deleted.

For each strategy, the change is:
```cpp
// Before:
auto rhythm = generate_musical_rhythm(totalBeats, pulse, rng);
// After:
PulseGenerator pgen(seed + 50);  // offset seed to avoid correlation with step RNG
PulseSequence rhythm = pgen.generate(totalBeats, pulse);
```

Then change the rhythm access pattern from `rhythm[i]` to `rhythm.get(i)` and `rhythm.size()` to `rhythm.count()`.

- [ ] **Step 3: Build and verify golden**

Same build + hash check. Expected: golden MAY change because CadentialApproach's rhythm generation now uses PulseGenerator instead of `generate_musical_rhythm`, and the triplet support + weighting changes produce different rhythms. If the hash drifts, re-pin:
```
cp renders/t3_check_1.wav renders/template_golden_phase1a.wav
sha256sum renders/template_golden_phase1a.wav > renders/template_golden_phase1a.sha256
```

- [ ] **Step 4: Commit**

```
git add engine/include/mforce/music/figures.h engine/include/mforce/music/shape_strategies.h
git add -f renders/template_golden_phase1a.wav renders/template_golden_phase1a.sha256  # only if re-pinned
git commit -m "feat(composer): add PulseGenerator with triplet group support

PulseGenerator generates PulseSequences of standard musical durations
including triplet groups (sixteenth, eighth, quarter triplets). Triplet
groups always emit 3 notes summing to a standard binary duration.
Biased toward defaultPulse with triplets slightly penalized to keep
them occasional. Replaces generate_musical_rhythm in shape strategies."
```

---

## Task 4: Motif variant expansion + JSON

**Files:**
- Modify: `engine/include/mforce/music/templates.h`
- Modify: `engine/include/mforce/music/templates_json.h`
- Modify: `engine/include/mforce/music/music_json.h`
- Modify: `engine/include/mforce/music/composer.h`

- [ ] **Step 1: Expand Motif struct in templates.h**

Find `struct Motif` (around line 135). Replace with:

```cpp
struct Motif {
    std::string name;

    using Content = std::variant<MelodicFigure, PulseSequence, StepSequence>;
    Content content;

    bool userProvided{false};
    uint32_t generationSeed{0};
    std::optional<FigureTemplate> constraints;

    bool is_figure() const { return std::holds_alternative<MelodicFigure>(content); }
    bool is_rhythm() const { return std::holds_alternative<PulseSequence>(content); }
    bool is_contour() const { return std::holds_alternative<StepSequence>(content); }

    const MelodicFigure& figure() const { return std::get<MelodicFigure>(content); }
    const PulseSequence& rhythm() const { return std::get<PulseSequence>(content); }
    const StepSequence& contour() const { return std::get<StepSequence>(content); }
};
```

Add `#include <variant>` at the top of `templates.h` if not already present.

- [ ] **Step 2: Add standalone PulseSequence and StepSequence JSON to music_json.h**

In `engine/include/mforce/music/music_json.h`, add (near the existing MelodicFigure serialization):

```cpp
// PulseSequence — serialized as a flat array of floats
inline void to_json(json& j, const PulseSequence& ps) {
  j = ps.pulses;
}
inline void from_json(const json& j, PulseSequence& ps) {
  ps.pulses.clear();
  for (const auto& v : j) ps.pulses.push_back(v.get<float>());
}

// StepSequence — serialized as a flat array of ints
inline void to_json(json& j, const StepSequence& ss) {
  j = ss.steps;
}
inline void from_json(const json& j, StepSequence& ss) {
  ss.steps.clear();
  for (const auto& v : j) ss.steps.push_back(v.get<int>());
}
```

- [ ] **Step 3: Update Motif JSON in templates_json.h**

Find the existing `to_json(json&, const Motif&)` and `from_json(const json&, Motif&)`. They currently read/write a `figure` field. Update to handle the variant:

```cpp
inline void to_json(json& j, const Motif& m) {
  j["name"] = m.name;
  if (m.is_figure()) {
    j["type"] = "figure";
    j["figure"] = std::get<MelodicFigure>(m.content);
  } else if (m.is_rhythm()) {
    j["type"] = "rhythm";
    j["rhythm"] = std::get<PulseSequence>(m.content);
  } else if (m.is_contour()) {
    j["type"] = "contour";
    j["contour"] = std::get<StepSequence>(m.content);
  }
  if (m.userProvided) j["userProvided"] = true;
  if (m.generationSeed != 0) j["generationSeed"] = m.generationSeed;
  if (m.constraints) j["constraints"] = *m.constraints;
}

inline void from_json(const json& j, Motif& m) {
  m.name = j.at("name").get<std::string>();
  std::string type = j.value("type", std::string("figure"));
  if (type == "rhythm") {
    PulseSequence ps;
    from_json(j.at("rhythm"), ps);
    m.content = std::move(ps);
  } else if (type == "contour") {
    StepSequence ss;
    from_json(j.at("contour"), ss);
    m.content = std::move(ss);
  } else {
    // Default: figure (backward compatible)
    if (j.contains("figure")) {
      MelodicFigure fig;
      from_json(j.at("figure"), fig);
      m.content = std::move(fig);
    } else {
      m.content = MelodicFigure{};  // empty figure if no content
    }
  }
  m.userProvided = j.value("userProvided", false);
  m.generationSeed = j.value("generationSeed", 0u);
  if (j.contains("constraints")) {
    FigureTemplate ft;
    from_json(j.at("constraints"), ft);
    m.constraints = ft;
  }
}
```

- [ ] **Step 4: Update realize_motifs_ in composer.h**

Find `Composer::realize_motifs_` in `composer.h`. Currently it iterates `tmpl.motifs`, checks if the motif is user-provided with a non-empty figure, and either copies the figure or generates one.

Update to handle the three variant types. For PulseSequence and StepSequence motifs, generate content when not user-provided:

```cpp
void realize_motifs_(const Piece& /*piece*/, const PieceTemplate& tmpl) {
  realizedMotifs_.clear();

  float sharedPulse = tmpl.defaultPulse;
  if (sharedPulse <= 0) {
    Randomizer pulseRng(rng_.rng());
    static const float pulses[] = {0.5f, 0.5f, 1.0f, 1.0f, 1.0f, 1.5f, 2.0f};
    sharedPulse = pulses[pulseRng.int_range(0, 6)];
  }

  for (auto& motif : tmpl.motifs) {
    if (motif.is_rhythm()) {
      // PulseSequence motif
      if (motif.userProvided || !motif.rhythm().pulses.empty()) {
        realizedRhythms_[motif.name] = motif.rhythm();
      } else {
        uint32_t s = motif.generationSeed ? motif.generationSeed : rng_.rng();
        float totalBeats = 4.0f;  // default; could come from constraints
        if (motif.constraints && motif.constraints->totalBeats > 0)
          totalBeats = motif.constraints->totalBeats;
        PulseGenerator pgen(s);
        realizedRhythms_[motif.name] = pgen.generate(totalBeats, sharedPulse);
      }
    } else if (motif.is_contour()) {
      // StepSequence motif
      if (motif.userProvided || !motif.contour().steps.empty()) {
        realizedContours_[motif.name] = motif.contour();
      } else {
        uint32_t s = motif.generationSeed ? motif.generationSeed : rng_.rng();
        int length = 4;  // default
        if (motif.constraints && motif.constraints->maxNotes > 0)
          length = motif.constraints->maxNotes;
        StepGenerator sgen(s);
        realizedContours_[motif.name] = sgen.random_sequence(length);
      }
    } else {
      // MelodicFigure motif (existing path, unchanged)
      if (motif.userProvided || !motif.figure().units.empty()) {
        realizedMotifs_[motif.name] = motif.figure();
      } else {
        uint32_t s = motif.generationSeed ? motif.generationSeed : rng_.rng();
        FigureTemplate ft = motif.constraints.value_or(FigureTemplate{});
        if (ft.defaultPulse <= 0) ft.defaultPulse = sharedPulse;
        DefaultFigureStrategy figStrat;
        realizedMotifs_[motif.name] = figStrat.generate_figure(ft, s);
      }
    }
  }
}
```

This requires adding two new private maps on `Composer`:

```cpp
std::unordered_map<std::string, PulseSequence> realizedRhythms_;
std::unordered_map<std::string, StepSequence> realizedContours_;
```

And two new public accessors:

```cpp
const PulseSequence* find_rhythm_motif(const std::string& name) const {
  auto it = realizedRhythms_.find(name);
  return it == realizedRhythms_.end() ? nullptr : &it->second;
}

const StepSequence* find_contour_motif(const std::string& name) const {
  auto it = realizedContours_.find(name);
  return it == realizedContours_.end() ? nullptr : &it->second;
}
```

- [ ] **Step 5: Build and verify golden**

Build + render + hash check. Expected: `8f7fc479...` unchanged. The golden template's motifs are all `type: "figure"` (implicitly, since `type` defaults to `"figure"` when absent). The existing MelodicFigure path is unchanged.

- [ ] **Step 6: Commit**

```
git add engine/include/mforce/music/figures.h engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h engine/include/mforce/music/music_json.h engine/include/mforce/music/composer.h
git commit -m "feat(composer): expand Motif to variant<MelodicFigure, PulseSequence, StepSequence>

Motif pool now holds three atom types: complete figures, rhythm-only
(PulseSequence), and contour-only (StepSequence). JSON type field
selects which ('figure' default, 'rhythm', 'contour'). realize_motifs_
generates atom motifs via PulseGenerator/StepGenerator when not
user-provided. Existing MelodicFigure motif path unchanged."
```

---

## Task 5: FigureTemplate motif reference fields + JSON

**Files:**
- Modify: `engine/include/mforce/music/templates.h`
- Modify: `engine/include/mforce/music/templates_json.h`

- [ ] **Step 1: Add fields to FigureTemplate**

Find `struct FigureTemplate` in `templates.h`. Add after the existing `literalNotes` field:

```cpp
    // --- For motif atom references ---
    std::string rhythmMotifName;            // name of a PulseSequence motif
    std::string contourMotifName;           // name of a StepSequence motif
    std::string rhythmTransform;            // transform to apply: "retrograde", "stretch", "compress"
    float rhythmTransformParam{0};          // factor for stretch/compress
    std::string contourTransform;           // transform to apply: "invert", "retrograde", "expand", "contract"
    float contourTransformParam{0};         // factor for expand/contract
```

- [ ] **Step 2: JSON round-trip in templates_json.h**

Find `to_json(json&, const FigureTemplate&)` and add after existing field emissions:

```cpp
if (!ft.rhythmMotifName.empty()) j["rhythmMotifName"] = ft.rhythmMotifName;
if (!ft.contourMotifName.empty()) j["contourMotifName"] = ft.contourMotifName;
if (!ft.rhythmTransform.empty()) j["rhythmTransform"] = ft.rhythmTransform;
if (ft.rhythmTransformParam != 0) j["rhythmTransformParam"] = ft.rhythmTransformParam;
if (!ft.contourTransform.empty()) j["contourTransform"] = ft.contourTransform;
if (ft.contourTransformParam != 0) j["contourTransformParam"] = ft.contourTransformParam;
```

Find `from_json(const json&, FigureTemplate&)` and add:

```cpp
ft.rhythmMotifName = j.value("rhythmMotifName", std::string(""));
ft.contourMotifName = j.value("contourMotifName", std::string(""));
ft.rhythmTransform = j.value("rhythmTransform", std::string(""));
ft.rhythmTransformParam = j.value("rhythmTransformParam", 0.0f);
ft.contourTransform = j.value("contourTransform", std::string(""));
ft.contourTransformParam = j.value("contourTransformParam", 0.0f);
```

- [ ] **Step 3: Build and verify golden**

Expected: `8f7fc479...` (unchanged — new fields default to empty/0, existing templates don't set them).

- [ ] **Step 4: Commit**

```
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h
git commit -m "feat(composer): add rhythmMotifName/contourMotifName + transform fields to FigureTemplate"
```

---

## Task 6: Strategy integration — motif atoms as optional inputs

**Files:**
- Modify: `engine/include/mforce/music/shape_strategies.h`
- Modify: `engine/include/mforce/music/composer.h` (if atom resolution happens in the dispatcher)

This task updates the three direction-aware strategies (Skipping, Stepping, CadentialApproach) to check for rhythm/contour motif references on the FigureTemplate, resolve them from the Composer's motif pools, apply any requested transforms, and use them as the basis for figure generation. When references are absent, strategies generate internally (current behavior).

- [ ] **Step 1: Add a motif-resolution helper**

In `shape_strategies.h` or as a free function in `composer.h`, add a helper that resolves + transforms motif atoms:

```cpp
namespace detail {

inline PulseSequence resolve_rhythm(const FigureTemplate& ft, StrategyContext& ctx,
                                     uint32_t seed, float totalBeats, float defaultPulse) {
  if (!ft.rhythmMotifName.empty()) {
    const PulseSequence* ps = ctx.composer->find_rhythm_motif(ft.rhythmMotifName);
    if (ps) {
      PulseSequence result = *ps;
      // Apply transform if requested
      if (ft.rhythmTransform == "retrograde") result = result.retrograded();
      else if (ft.rhythmTransform == "stretch") result = result.stretched(ft.rhythmTransformParam > 0 ? ft.rhythmTransformParam : 2.0f);
      else if (ft.rhythmTransform == "compress") result = result.compressed(ft.rhythmTransformParam > 0 ? ft.rhythmTransformParam : 2.0f);
      return result;
    }
  }
  // No motif reference or not found — generate
  PulseGenerator pgen(seed + 50);
  return pgen.generate(totalBeats, defaultPulse);
}

inline StepSequence resolve_contour(const FigureTemplate& ft, StrategyContext& ctx,
                                     uint32_t seed, int noteCount, FigureDirection dir) {
  if (!ft.contourMotifName.empty()) {
    const StepSequence* ss = ctx.composer->find_contour_motif(ft.contourMotifName);
    if (ss) {
      StepSequence result = *ss;
      if (ft.contourTransform == "invert") result = result.inverted();
      else if (ft.contourTransform == "retrograde") result = result.retrograded();
      else if (ft.contourTransform == "expand") result = result.expanded(ft.contourTransformParam > 0 ? ft.contourTransformParam : 2.0f);
      else if (ft.contourTransform == "contract") result = result.contracted(ft.contourTransformParam > 0 ? ft.contourTransformParam : 2.0f);
      return result;
    }
  }
  // No motif reference or not found — generate using direction
  StepSequence ss;
  Randomizer stepRng(seed);
  int totalSteps = noteCount - 1;
  for (int i = 0; i < noteCount; ++i) {
    if (i == 0) {
      ss.add(0);
    } else {
      int sign = direction_sign(dir, i - 1, totalSteps, stepRng);
      ss.add(sign);  // magnitude 1 for stepping; caller can scale for skipping
    }
  }
  return ss;
}

} // namespace detail
```

- [ ] **Step 2: Update ShapeSkippingStrategy**

In `shape_strategies.h`, find `ShapeSkippingStrategy::realize_figure`. Replace its body to use the resolution helpers:

```cpp
inline MelodicFigure ShapeSkippingStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  Randomizer rng(seed);
  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  // Resolve rhythm (from motif or generated)
  PulseSequence rhythm = detail::resolve_rhythm(ft, ctx, seed, totalBeats, pulse);
  int noteCount = rhythm.count();
  if (noteCount == 0) return MelodicFigure{};

  // Resolve contour (from motif or generated)
  StepSequence contour = detail::resolve_contour(ft, ctx, seed, noteCount, ft.direction);

  // Scale contour magnitudes for skipping (thirds/fourths)
  // If contour came from a motif, its magnitudes are already set — don't rescale.
  if (ft.contourMotifName.empty()) {
    Randomizer magRng(seed + 77);
    for (int i = 0; i < contour.count(); ++i) {
      if (contour.steps[i] != 0) {
        int sign = (contour.steps[i] > 0) ? 1 : -1;
        int mag = magRng.decide(0.5f) ? 2 : 3;
        contour.steps[i] = sign * mag;
      }
    }
  }

  // Adjust for targetNet
  if (ft.targetNet != 0 && contour.count() > 1) {
    int diff = ft.targetNet - contour.net();
    contour.steps.back() += diff;
  }

  // Pad/trim contour to match rhythm length
  while (contour.count() < rhythm.count()) contour.add(0);
  while (contour.count() > rhythm.count()) contour.steps.pop_back();

  return MelodicFigure::from_atoms(rhythm, contour);
}
```

- [ ] **Step 3: Update ShapeSteppingStrategy similarly**

Same pattern but step magnitudes stay at 1 (no rescaling):

```cpp
inline MelodicFigure ShapeSteppingStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;

  PulseSequence rhythm = detail::resolve_rhythm(ft, ctx, seed, totalBeats, pulse);
  int noteCount = rhythm.count();
  if (noteCount == 0) return MelodicFigure{};

  StepSequence contour = detail::resolve_contour(ft, ctx, seed, noteCount, ft.direction);

  if (ft.targetNet != 0 && contour.count() > 1) {
    int diff = ft.targetNet - contour.net();
    contour.steps.back() += diff;
  }

  while (contour.count() < rhythm.count()) contour.add(0);
  while (contour.count() > rhythm.count()) contour.steps.pop_back();

  return MelodicFigure::from_atoms(rhythm, contour);
}
```

- [ ] **Step 4: Update ShapeCadentialApproachStrategy**

CadentialApproach applies functional coupling: the arrival note should be long. When a rhythm motif is provided, the strategy respects it but may lengthen the last pulse:

```cpp
inline MelodicFigure ShapeCadentialApproachStrategy::realize_figure(
    const FigureTemplate& ft, StrategyContext& ctx) {
  uint32_t seed = ft.seed ? ft.seed : ctx.rng->rng();
  Randomizer rng(seed);
  float totalBeats = (ft.totalBeats > 0) ? ft.totalBeats : 4.0f;
  float pulse = (ft.defaultPulse > 0) ? ft.defaultPulse : 1.0f;
  int approachDir = (ft.shapeDirection < 0) ? -1 : 1;
  int targetSteps = (ft.targetNet != 0) ? ft.targetNet :
                    ((ft.shapeParam > 0) ? ft.shapeParam * (-approachDir) : -3 * approachDir);

  // Resolve rhythm
  PulseSequence rhythm = detail::resolve_rhythm(ft, ctx, seed, totalBeats, pulse);

  // Functional coupling: ensure last note is at least double the average
  if (rhythm.count() >= 2) {
    float avg = rhythm.total_length() / rhythm.count();
    if (rhythm.pulses.back() < avg * 1.5f) {
      // Steal time from approach notes to lengthen arrival
      float steal = avg;
      rhythm.pulses.back() += steal;
      // Distribute the stolen time evenly from earlier notes
      int donors = rhythm.count() - 1;
      if (donors > 0) {
        float perDonor = steal / donors;
        for (int i = 0; i < donors; ++i) {
          rhythm.pulses[i] = std::max(0.125f, rhythm.pulses[i] - perDonor);
        }
      }
    }
  }

  int noteCount = rhythm.count();
  if (noteCount == 0) return MelodicFigure{};

  // Generate approach contour
  StepSequence contour;
  int stepsRemaining = targetSteps;
  bool overshoot = rng.decide(0.3f) && noteCount >= 3;

  for (int i = 0; i < noteCount; ++i) {
    if (i == 0) {
      contour.add(0);
    } else if (i == noteCount - 1) {
      contour.add(stepsRemaining);  // arrival: whatever's left
    } else if (overshoot && i == 1) {
      int over = -approachDir * (rng.decide(0.5f) ? 1 : 2);
      contour.add(over);
      stepsRemaining -= over;
    } else {
      int stepSize = rng.decide(0.7f) ? 1 : 2;
      int s = (stepsRemaining > 0) ? stepSize : (stepsRemaining < 0) ? -stepSize : 0;
      contour.add(s);
      stepsRemaining -= s;
    }
  }

  // If a contour motif was provided, use it instead (override the generated one)
  if (!ft.contourMotifName.empty()) {
    const StepSequence* ss = ctx.composer->find_contour_motif(ft.contourMotifName);
    if (ss) {
      contour = *ss;
      if (ft.contourTransform == "invert") contour = contour.inverted();
      else if (ft.contourTransform == "retrograde") contour = contour.retrograded();
      else if (ft.contourTransform == "expand") contour = contour.expanded(ft.contourTransformParam > 0 ? ft.contourTransformParam : 2.0f);
      else if (ft.contourTransform == "contract") contour = contour.contracted(ft.contourTransformParam > 0 ? ft.contourTransformParam : 2.0f);
      // Adjust last step for targetNet
      if (ft.targetNet != 0 && contour.count() > 1) {
        int diff = ft.targetNet - contour.net();
        contour.steps.back() += diff;
      }
    }
  }

  while (contour.count() < rhythm.count()) contour.add(0);
  while (contour.count() > rhythm.count()) contour.steps.pop_back();

  return MelodicFigure::from_atoms(rhythm, contour);
}
```

- [ ] **Step 5: Build and verify golden**

Build + hash check. If the strategy rewrites changed how CadentialApproach generates figures for the golden template (which uses MelodicFunction::Cadential → CadentialApproach shape), the hash MAY drift. Re-pin if needed.

- [ ] **Step 6: Commit**

```
git add engine/include/mforce/music/shape_strategies.h engine/include/mforce/music/composer.h
git add -f renders/template_golden_phase1a.wav renders/template_golden_phase1a.sha256  # only if re-pinned
git commit -m "feat(composer): strategies accept motif atoms as optional inputs

Skipping, Stepping, and CadentialApproach now check FigureTemplate's
rhythmMotifName/contourMotifName. When present, the named atom is
looked up from the Composer's motif pool, optionally transformed
(invert, retrograde, expand, contract for contour; retrograde, stretch,
compress for rhythm), and used as the basis for figure generation.
When absent, strategies generate internally (unchanged behavior).

CadentialApproach applies functional coupling: arrival note is
lengthened regardless of the rhythm motif's durations."
```

---

## Task 7: K467 template with atom motifs + render

**Files:**
- Create: `patches/test_k467_motifs.json`

Author a new K467 template that uses the atom motif system for coherence: bars 1+3 share a rhythm motif, bars 2+4 share a rhythm motif, bar 3's contour inverts bar 1's contour.

- [ ] **Step 1: Author the template**

Create `patches/test_k467_motifs.json`:

```json
{
  "_comment": "K467/i opening — atom motif coherence test. Bars 1+3 share arpeggio_rhythm, bars 2+4 share cadence_rhythm. Bar 3 contour is inversion of bar 1's.",
  "keyName": "C",
  "scaleName": "Major",
  "bpm": 100.0,
  "masterSeed": 467,
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
    },
    {
      "name": "arpeggio_contour",
      "type": "contour",
      "contour": [0, -3, 3, 2],
      "userProvided": true
    }
  ],
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
                  "source": "generate",
                  "shape": "skipping",
                  "rhythmMotifName": "arpeggio_rhythm",
                  "contourMotifName": "arpeggio_contour",
                  "totalBeats": 4.0
                },
                {
                  "source": "generate",
                  "shape": "cadential_approach",
                  "rhythmMotifName": "cadence_rhythm",
                  "shapeDirection": -1,
                  "shapeParam": 5,
                  "totalBeats": 4.0
                }
              ]
            },
            {
              "name": "Phrase2",
              "cadenceType": 2,
              "cadenceTarget": 2,
              "figures": [
                {
                  "source": "generate",
                  "shape": "skipping",
                  "rhythmMotifName": "arpeggio_rhythm",
                  "contourMotifName": "arpeggio_contour",
                  "contourTransform": "invert",
                  "totalBeats": 4.0
                },
                {
                  "source": "generate",
                  "shape": "cadential_approach",
                  "rhythmMotifName": "cadence_rhythm",
                  "shapeDirection": -1,
                  "shapeParam": 3,
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

Key coherence features:
- `arpeggio_rhythm` shared between bars 1 and 3 (same quarter-note rhythm)
- `cadence_rhythm` shared between bars 2 and 4 (same dotted-quarter + triplet pattern)
- `arpeggio_contour` shared, but bar 3 uses `contourTransform: "invert"` to flip the direction
- All motifs are `userProvided: true` (literal content, not generated)

- [ ] **Step 2: Render 3 WAVs**

```
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/TriTest.json renders/k467_motifs 3 --template patches/test_k467_motifs.json
```

Expected: 3 WAVs. Since all motifs are user-provided and the contour motif determines pitches, the arpeggio figures should be IDENTICAL in rhythm and use related contours (bar 1 = original, bar 3 = inverted). The cadential figures share rhythm but have independently generated approach paths (varying across the 3 seeds).

- [ ] **Step 3: Inspect composed JSON**

```
python -c "
import json
for i in range(1,4):
  with open(f'renders/k467_motifs_{i}.json') as f: p = json.load(f)
  print(f'=== Render {i} ===')
  for part in p['parts']:
    for pname, passage in part['passages'].items():
      for pi, phr in enumerate(passage['phrases']):
        for fi, fig in enumerate(phr['figures']):
          steps = [u['step'] for u in fig['units']]
          durs = [round(u['duration'],3) for u in fig['units']]
          print(f'  p{pi}f{fi}: steps={steps} durs={durs}')
"
```

Expected across all 3 renders:
- `p0f0` (bar 1): steps = `[0, -3, 3, 2]`, durs = `[1.0, 1.0, 1.0, 1.0]` — SAME every render (motif is literal)
- `p1f0` (bar 3): steps = `[0, 3, -3, -2]`, durs = `[1.0, 1.0, 1.0, 1.0]` — SAME every render (inverted contour + same rhythm)
- `p0f1` (bar 2): durs = `[1.5, 0.167, 0.167, 0.167, 1.0, 1.0]` — SAME every render (motif rhythm), steps VARY per seed (generated approach path)
- `p1f1` (bar 4): durs = `[1.5, 0.167, 0.167, 0.167, 1.0, 1.0]` — SAME rhythm, steps VARY per seed

If bars 1+3 don't share identical rhythm, motif resolution failed. If bar 3's steps aren't the sign-flip of bar 1's, the invert transform failed.

- [ ] **Step 4: Commit**

```
git add patches/test_k467_motifs.json
git commit -m "test(composer): K467 opening with atom motifs for coherence

Bars 1+3 share arpeggio_rhythm (quarter notes) and arpeggio_contour
(bar 3 inverts bar 1's contour). Bars 2+4 share cadence_rhythm
(dotted-quarter + triplet descent pattern). Cadential approach paths
vary per seed while rhythm stays locked to the motif. Tests the
full motif-atom pipeline: variant Motif pool, resolve_rhythm/contour
helpers, atom transforms, strategy integration."
```

---

## Exit criteria

1. `cmake --build` succeeds from a clean tree.
2. `StepSequence` has 4 transform methods; `PulseSequence` has 3.
3. `MelodicFigure` has `extract_pulses()`, `extract_steps()`, and `from_atoms()`.
4. `PulseGenerator` exists with triplet group support.
5. `Motif` uses `std::variant<MelodicFigure, PulseSequence, StepSequence>`.
6. `FigureTemplate` has `rhythmMotifName`/`contourMotifName` + transform fields.
7. Skipping/Stepping/CadentialApproach accept optional motif inputs.
8. K467 motif template renders correctly with shared rhythm and inverted contour.
9. Golden hash preserved (or re-pinned with documented reason).
10. Commit log shows 7 content commits in order.

## What is NOT in this plan

- No `vary`, `truncate`, or `tail` transforms.
- No complex triplet scenarios (ties to adjacent notes).
- No OutlineFigure/OutlinePassage strategies.
- No FigureConnector resurrection.
- No harmony-aware composition.
- No changes to the MelodicFigure motif realization path (Phase 1a bypass stays).
- No changes to Period/Sentence strategy configs (still hold `MelodicFigure` not `FigureTemplate`).
- No audition — mechanical verification only. Matt audits the K467 motif WAVs on return.
