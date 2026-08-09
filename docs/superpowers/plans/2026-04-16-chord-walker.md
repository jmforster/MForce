# ChordWalker Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a constrained chord-progression engine ("ChordWalker") that generates style-appropriate progressions from JSON transition tables, and wire it into the Composer pipeline with per-passage harmony ownership.

**Architecture:** A `StyleTable` loads from JSON, encoding first-order chord transitions plus sparse higher-order overrides (variable-order Markov with back-off). `ChordWalker::walk()` traverses the graph under start/end/beat constraints. `PassageStrategy` declares a `StrategyScope` (Melody vs MelodyAndHarmony). Composer checks scope before deciding whether to pre-populate harmony. Section gains a `HarmonyTimeline` assembled from passage-level segments.

**Tech Stack:** C++20 header-only (inline in `engine/include/mforce/music/`), nlohmann/json, existing `ScaleChord`/`ChordProgression` types. JSON style tables in `styles/`.

---

## File Structure

| Action | File | Responsibility |
|--------|------|----------------|
| Create | `styles/classical_mozart.json` | First style table — common-practice diatonic transitions |
| Create | `engine/include/mforce/music/style_table.h` | `StyleTable` struct + JSON loader + `ChordLabel` parser |
| Create | `engine/include/mforce/music/chord_walker.h` | `WalkConstraint` + `ChordWalker::walk()` — constrained progression generator |
| Create | `engine/include/mforce/music/harmony_timeline.h` | `HarmonySegment` + `HarmonyTimeline` — per-beat chord lookup |
| Modify | `engine/include/mforce/music/strategy.h` | Add `StrategyScope` enum + virtual `scope()` to `PassageStrategy` |
| Modify | `engine/include/mforce/music/structure.h` | Add `HarmonyTimeline` to `Section` |
| Modify | `engine/include/mforce/music/locus.h` | Add `const HarmonyTimeline*` for strategy access |
| Modify | `engine/include/mforce/music/composer.h` | Wire ChordWalker into `setup_piece_`, respect `StrategyScope` |
| Modify | `engine/include/mforce/music/period_passage_strategy.h` | Override `scope()` to `MelodyAndHarmony`, call ChordWalker with cadence pins |
| Modify | `engine/include/mforce/music/templates.h` | Add `styleName` to `SectionDef` |
| Modify | `engine/include/mforce/music/templates_json.h` | JSON support for `styleName` |
| Create | `patches/test_k467_walker.json` | Smoke test: K467 bars 1-12 using ChordWalker-generated harmony |

---

### Task 1: ChordLabel Parser + StyleTable

**Files:**
- Create: `engine/include/mforce/music/style_table.h`

This task creates the data model for style tables and the parser that converts chord label strings (`"I"`, `"V7"`, `"bVII"`) into `ScaleChord` objects.

- [ ] **Step 1: Create style_table.h with ChordLabel parser**

```cpp
// engine/include/mforce/music/style_table.h
#pragma once
#include "mforce/music/basics.h"
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <optional>
#include <stdexcept>
#include <algorithm>
#include <sstream>
#include <fstream>

namespace mforce {

using json = nlohmann::json;

// ---------------------------------------------------------------------------
// ChordLabel — parse Roman-numeral chord labels into ScaleChord.
//
// Format: [b|#]<Roman>[quality]
//   Roman: I ii III iv V vi vii (case encodes major/minor default)
//   Quality override: M, m, 7, M7, m7, dim, aug, o (= dim), + (= aug)
//   Alteration prefix: b = flat (-1), # = sharp (+1)
//
// Examples: "I" → degree 0 Major, "V7" → degree 4 Dom7, "bVII" → degree 6 alt -1 Major
// ---------------------------------------------------------------------------
struct ChordLabel {
  static ScaleChord parse(const std::string& label) {
    if (label.empty()) throw std::runtime_error("Empty chord label");

    int pos = 0;
    int alteration = 0;

    // Leading accidental
    if (label[pos] == 'b') { alteration = -1; ++pos; }
    else if (label[pos] == '#') { alteration = 1; ++pos; }

    // Roman numeral → degree + default quality
    int degree = -1;
    bool defaultMinor = false;
    std::string roman;

    // Collect roman chars (upper or lower i, v, I, V)
    while (pos < (int)label.size()) {
      char c = label[pos];
      char cu = (char)std::toupper((unsigned char)c);
      if (cu != 'I' && cu != 'V') break;
      roman += c;
      ++pos;
    }

    if (roman.empty()) throw std::runtime_error("No Roman numeral in chord label: " + label);

    // Determine degree from roman (case-insensitive for degree, case for quality)
    std::string upper = roman;
    for (auto& ch : upper) ch = (char)std::toupper((unsigned char)ch);

    if      (upper == "I")    degree = 0;
    else if (upper == "II")   degree = 1;
    else if (upper == "III")  degree = 2;
    else if (upper == "IV")   degree = 3;
    else if (upper == "V")    degree = 4;
    else if (upper == "VI")   degree = 5;
    else if (upper == "VII")  degree = 6;
    else throw std::runtime_error("Unknown Roman numeral: " + roman + " in " + label);

    // Default quality from case: lowercase = minor, uppercase = major
    defaultMinor = std::islower((unsigned char)roman[0]);

    // Quality suffix (rest of string after roman)
    std::string qualitySuffix = label.substr(pos);
    std::string qualityName;

    if (qualitySuffix.empty()) {
      qualityName = defaultMinor ? "Minor" : "Major";
    } else if (qualitySuffix == "M")   { qualityName = "Major"; }
    else if (qualitySuffix == "m")     { qualityName = "Minor"; }
    else if (qualitySuffix == "7")     { qualityName = "7"; }
    else if (qualitySuffix == "M7")    { qualityName = "Major7"; }
    else if (qualitySuffix == "m7")    { qualityName = "Minor7"; }
    else if (qualitySuffix == "dim" || qualitySuffix == "o")  { qualityName = "Diminished"; }
    else if (qualitySuffix == "aug" || qualitySuffix == "+")  { qualityName = "Augmented"; }
    else {
      // Try direct lookup
      qualityName = qualitySuffix;
    }

    ScaleChord sc;
    sc.degree = degree;
    sc.alteration = alteration;
    sc.quality = &ChordDef::get(qualityName);
    return sc;
  }

  // Convert ScaleChord back to a label string (for JSON round-trip / debugging)
  static std::string to_string(const ScaleChord& sc) {
    static const char* romans[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char* romansLower[] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};

    std::string result;
    if (sc.alteration == -1) result += 'b';
    else if (sc.alteration == 1) result += '#';

    bool isMinor = sc.quality && (sc.quality->name == "Minor" || sc.quality->name == "Minor7");
    int deg = sc.degree % 7;
    result += isMinor ? romansLower[deg] : romans[deg];

    if (sc.quality) {
      const auto& qn = sc.quality->name;
      if (qn == "7") result += "7";
      else if (qn == "Major7") result += "M7";
      else if (qn == "Minor7") result += "m7";
      else if (qn == "Diminished") result += "o";
      else if (qn == "Augmented") result += "+";
      // Major/Minor already encoded in case
    }
    return result;
  }
};

// ---------------------------------------------------------------------------
// StyleTable — chord transition graph with variable-order back-off.
// ---------------------------------------------------------------------------
struct StyleTable {
  struct Transition {
    ScaleChord target;
    float weight{1.0f};
  };

  std::string name;
  std::string description;

  // First-order transitions: key = ChordLabel string, value = [(target, weight)]
  std::unordered_map<std::string, std::vector<Transition>> transitions;

  // Higher-order overrides: key = "prev,curr" (temporal order), value = [(target, weight)]
  // Longest prefix match wins; back-off to first-order if no match.
  std::unordered_map<std::string, std::vector<Transition>> overrides;

  // Preferred harmonic rhythm in beats (0 = let walker decide)
  float preferredChordBeats{0.0f};

  // Query: get transitions for a chord given history.
  // Returns override if history matches, else first-order, else empty.
  const std::vector<Transition>* lookup(const std::string& currentLabel,
                                         const std::vector<std::string>& history) const {
    // Try longest match first: "prev,curr"
    if (!history.empty()) {
      std::string key = history.back() + "," + currentLabel;
      auto it = overrides.find(key);
      if (it != overrides.end()) return &it->second;
    }
    // Fall back to first-order
    auto it = transitions.find(currentLabel);
    if (it != transitions.end()) return &it->second;
    return nullptr;
  }

  // Load from JSON file
  static StyleTable load(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open()) throw std::runtime_error("Cannot open style table: " + path);
    json j = json::parse(f);
    return from_json(j);
  }

  // Load by name — searches styles/ directory
  static StyleTable load_by_name(const std::string& styleName) {
    // Try styles/<name>.json
    std::string path = "styles/" + styleName + ".json";
    return load(path);
  }

  static StyleTable from_json(const json& j) {
    StyleTable st;
    st.name = j.value("name", "");
    st.description = j.value("description", "");
    st.preferredChordBeats = j.value("preferredChordBeats", 0.0f);

    if (j.contains("transitions")) {
      for (auto& [label, arr] : j["transitions"].items()) {
        std::vector<Transition> ts;
        for (auto& entry : arr) {
          Transition t;
          t.target = ChordLabel::parse(entry[0].get<std::string>());
          t.weight = entry[1].get<float>();
          ts.push_back(t);
        }
        st.transitions[label] = std::move(ts);
      }
    }

    if (j.contains("overrides")) {
      for (auto& [key, arr] : j["overrides"].items()) {
        std::vector<Transition> ts;
        for (auto& entry : arr) {
          Transition t;
          t.target = ChordLabel::parse(entry[0].get<std::string>());
          t.weight = entry[1].get<float>();
          ts.push_back(t);
        }
        st.overrides[key] = std::move(ts);
      }
    }

    return st;
  }
};

} // namespace mforce
```

- [ ] **Step 2: Build to verify compilation**

Run:
```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/tools/mforce_cli/mforce_cli.vcxproj -p:Configuration=Release -nologo -v:minimal
```

Note: style_table.h is header-only; include it from composer.h (Task 6) to verify. For now, verify standalone compilation by adding a temporary include to an existing file that includes nlohmann/json. A quick way: include it from `templates_json.h` temporarily, or create a minimal test.

For this step, just verify no syntax errors by adding at the top of `templates_json.h`:

```cpp
#include "mforce/music/style_table.h"
```

Build. Expected: clean compile (warnings OK).

- [ ] **Step 3: Commit**

```bash
git add engine/include/mforce/music/style_table.h
git commit -m "feat(harmony): ChordLabel parser + StyleTable with VOM back-off"
```

---

### Task 2: Classical Mozart Style Table (JSON)

**Files:**
- Create: `styles/classical_mozart.json`

- [ ] **Step 1: Create the styles directory and JSON file**

```bash
mkdir -p styles
```

```json
{
  "name": "classical_mozart",
  "description": "Common-practice diatonic transitions, Mozart-era. Strong tonic-dominant polarity. Secondary dominants rare in opening periods.",
  "preferredChordBeats": 4.0,
  "transitions": {
    "I":    [["IV", 3], ["V", 5], ["vi", 2], ["ii", 2], ["iii", 1], ["V7", 3]],
    "ii":   [["V", 5], ["V7", 4], ["viio", 1]],
    "iii":  [["vi", 3], ["IV", 2]],
    "IV":   [["V", 5], ["V7", 4], ["I", 2], ["ii", 1]],
    "V":    [["I", 6], ["vi", 2]],
    "V7":   [["I", 7], ["vi", 2]],
    "vi":   [["ii", 3], ["IV", 3], ["V", 2], ["V7", 1]],
    "viio": [["I", 5], ["vi", 2]]
  },
  "overrides": {
    "I,vi":  [["ii", 5], ["IV", 2], ["V", 1]],
    "V,vi":  [["IV", 5], ["ii", 2], ["I", 1]],
    "IV,V":  [["I", 7], ["vi", 2]],
    "IV,V7": [["I", 7], ["vi", 2]],
    "ii,V":  [["I", 6], ["vi", 2]],
    "ii,V7": [["I", 7], ["vi", 1]]
  }
}
```

- [ ] **Step 2: Verify JSON parses**

Add a quick load check — from the repo root, run:

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --help
```

(We'll add actual loading in Task 6. For now, just verify the JSON is well-formed by eyeballing or using `python -m json.tool styles/classical_mozart.json`.)

- [ ] **Step 3: Commit**

```bash
git add styles/classical_mozart.json
git commit -m "data(harmony): classical_mozart style table for ChordWalker"
```

---

### Task 3: ChordWalker — Constrained Walk Engine

**Files:**
- Create: `engine/include/mforce/music/chord_walker.h`

The walk algorithm: start at a chord, at each step sample the next chord from the style table (respecting history), assign a duration, steer toward the end-chord as remaining beats shrink, force the last chord to the end target.

- [ ] **Step 1: Create chord_walker.h**

```cpp
// engine/include/mforce/music/chord_walker.h
#pragma once
#include "mforce/music/style_table.h"
#include "mforce/music/figures.h"
#include "mforce/core/randomizer.h"
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// WalkConstraint — what the caller (Composer or strategy) provides.
// ---------------------------------------------------------------------------
struct WalkConstraint {
  ScaleChord startChord;
  std::optional<ScaleChord> endChord;   // cadence target (if any)
  float totalBeats{16.0f};
  float minChordBeats{2.0f};
  float maxChordBeats{8.0f};
};

// ---------------------------------------------------------------------------
// ChordWalker — generates a ChordProgression by walking a StyleTable.
// ---------------------------------------------------------------------------
struct ChordWalker {

  // Walk the style table under constraints, producing a ChordProgression.
  static ChordProgression walk(const StyleTable& style,
                               const WalkConstraint& constraint,
                               uint32_t seed) {
    Randomizer rng(seed);
    ChordProgression prog;

    float remaining = constraint.totalBeats;
    ScaleChord current = constraint.startChord;
    std::string currentLabel = ChordLabel::to_string(current);
    std::vector<std::string> history;

    while (remaining > 0.01f) {
      // Determine chord duration
      float chordBeats = pick_duration(style, constraint, remaining, rng);

      // If this is the last chord and we have an end target, force it
      float afterThis = remaining - chordBeats;
      if (constraint.endChord && afterThis < constraint.minChordBeats) {
        // This IS the last chord — force end target
        prog.add(constraint.endChord.value(), remaining);
        remaining = 0;
        break;
      }

      prog.add(current, chordBeats);
      remaining -= chordBeats;
      history.push_back(currentLabel);

      if (remaining < 0.01f) break;

      // Pick next chord
      current = pick_next(style, currentLabel, history, constraint, remaining, rng);
      currentLabel = ChordLabel::to_string(current);
    }

    return prog;
  }

private:
  static float pick_duration(const StyleTable& style,
                             const WalkConstraint& constraint,
                             float remaining,
                             Randomizer& rng) {
    float preferred = style.preferredChordBeats > 0
                      ? style.preferredChordBeats
                      : constraint.minChordBeats;

    float lo = constraint.minChordBeats;
    float hi = std::min(constraint.maxChordBeats, remaining);
    if (lo > hi) lo = hi;

    // Prefer the style's harmonic rhythm, with some variation
    float base = std::clamp(preferred, lo, hi);
    float variation = rng.decide(0.3f) ? (rng.decide(0.5f) ? 0.5f : 2.0f) : 1.0f;
    float dur = std::clamp(base * variation, lo, hi);

    // Snap to whole beats for clean harmonic rhythm
    dur = std::round(dur);
    if (dur < lo) dur = lo;
    if (dur > remaining) dur = remaining;

    return dur;
  }

  static ScaleChord pick_next(const StyleTable& style,
                              const std::string& currentLabel,
                              const std::vector<std::string>& history,
                              const WalkConstraint& constraint,
                              float remaining,
                              Randomizer& rng) {
    const auto* transitions = style.lookup(currentLabel, history);

    if (!transitions || transitions->empty()) {
      // No transitions available — return to tonic
      return ScaleChord{0, 0, &ChordDef::get("Major")};
    }

    // Build weighted list, with approach boost if nearing end
    std::vector<float> weights;
    weights.reserve(transitions->size());

    bool approaching = constraint.endChord.has_value()
                       && remaining <= constraint.minChordBeats * 3;

    for (const auto& t : *transitions) {
      float w = t.weight;

      if (approaching && constraint.endChord) {
        // Boost chords that can reach the target in 1 step
        // (i.e., this chord IS the pre-cadence chord)
        if (can_reach_target(style, ChordLabel::to_string(t.target),
                             *constraint.endChord)) {
          w *= 3.0f;
        }
        // Also boost the target itself if it appears
        if (t.target.degree == constraint.endChord->degree
            && t.target.alteration == constraint.endChord->alteration) {
          w *= 5.0f;
        }
      }

      weights.push_back(w);
    }

    // Weighted random selection
    float total = 0;
    for (float w : weights) total += w;
    if (total <= 0) return (*transitions)[0].target;

    float roll = rng.value() * total;
    float accum = 0;
    for (int i = 0; i < (int)transitions->size(); ++i) {
      accum += weights[i];
      if (roll <= accum) return (*transitions)[i].target;
    }

    return transitions->back().target;
  }

  // Check if `fromLabel` has a transition to `target` in the style table.
  static bool can_reach_target(const StyleTable& style,
                               const std::string& fromLabel,
                               const ScaleChord& target) {
    auto it = style.transitions.find(fromLabel);
    if (it == style.transitions.end()) return false;
    for (const auto& t : it->second) {
      if (t.target.degree == target.degree
          && t.target.alteration == target.alteration) {
        return true;
      }
    }
    return false;
  }
};

} // namespace mforce
```

- [ ] **Step 2: Build to verify compilation**

Add a temporary include of `chord_walker.h` in `templates_json.h` (alongside style_table.h from Task 1):

```cpp
#include "mforce/music/chord_walker.h"
```

Run:
```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/tools/mforce_cli/mforce_cli.vcxproj -p:Configuration=Release -nologo -v:minimal
```

Expected: clean compile.

- [ ] **Step 3: Commit**

```bash
git add engine/include/mforce/music/chord_walker.h
git commit -m "feat(harmony): ChordWalker constrained walk engine"
```

---

### Task 4: HarmonyTimeline

**Files:**
- Create: `engine/include/mforce/music/harmony_timeline.h`
- Modify: `engine/include/mforce/music/structure.h`

- [ ] **Step 1: Create harmony_timeline.h**

```cpp
// engine/include/mforce/music/harmony_timeline.h
#pragma once
#include "mforce/music/figures.h"
#include <vector>
#include <string>
#include <algorithm>
#include <optional>

namespace mforce {

// ---------------------------------------------------------------------------
// HarmonySegment — a passage-owned slice of the harmonic timeline.
// ---------------------------------------------------------------------------
struct HarmonySegment {
  float startBeat{0.0f};
  float endBeat{0.0f};
  ChordProgression progression;
  std::string ownerStrategy;
};

// ---------------------------------------------------------------------------
// HarmonyTimeline — ordered, non-overlapping segments covering a Section.
// ---------------------------------------------------------------------------
struct HarmonyTimeline {
  std::vector<HarmonySegment> segments;

  // What chord is active at a given beat? Returns nullptr if no harmony covers that beat.
  const ScaleChord* chord_at(float beat) const {
    for (const auto& seg : segments) {
      if (beat < seg.startBeat || beat >= seg.endBeat) continue;
      // Walk through the segment's progression to find the active chord
      float b = seg.startBeat;
      for (int i = 0; i < seg.progression.count(); ++i) {
        float dur = seg.progression.pulses.get(i);
        if (beat >= b && beat < b + dur) {
          return &seg.progression.chords.get(i);
        }
        b += dur;
      }
    }
    return nullptr;
  }

  // Get the full progression for a time range (returns chords overlapping [start, end)).
  ChordProgression slice(float startBeat, float endBeat) const {
    ChordProgression result;
    for (const auto& seg : segments) {
      if (seg.endBeat <= startBeat || seg.startBeat >= endBeat) continue;
      float b = seg.startBeat;
      for (int i = 0; i < seg.progression.count(); ++i) {
        float dur = seg.progression.pulses.get(i);
        float chordStart = b;
        float chordEnd = b + dur;
        if (chordEnd > startBeat && chordStart < endBeat) {
          float clippedStart = std::max(chordStart, startBeat);
          float clippedEnd = std::min(chordEnd, endBeat);
          result.add(seg.progression.chords.get(i), clippedEnd - clippedStart);
        }
        b += dur;
      }
    }
    return result;
  }

  // Replace the segment for [start, end). Removes any overlapping segments.
  void set_segment(float startBeat, float endBeat,
                   ChordProgression prog, const std::string& owner) {
    // Remove overlapping segments
    segments.erase(
      std::remove_if(segments.begin(), segments.end(),
        [&](const HarmonySegment& s) {
          return s.startBeat < endBeat && s.endBeat > startBeat;
        }),
      segments.end());

    segments.push_back({startBeat, endBeat, std::move(prog), owner});

    // Keep sorted by startBeat
    std::sort(segments.begin(), segments.end(),
      [](const HarmonySegment& a, const HarmonySegment& b) {
        return a.startBeat < b.startBeat;
      });
  }

  bool empty() const { return segments.empty(); }
};

} // namespace mforce
```

- [ ] **Step 2: Add HarmonyTimeline to Section**

In `engine/include/mforce/music/structure.h`, add the include and the field:

Add after the existing includes at the top:
```cpp
#include "mforce/music/harmony_timeline.h"
```

Add to `struct Section`, after `std::optional<ChordProgression> chordProgression;` (line 153):
```cpp
  HarmonyTimeline harmonyTimeline;
```

- [ ] **Step 3: Build to verify**

Run:
```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/tools/mforce_cli/mforce_cli.vcxproj -p:Configuration=Release -nologo -v:minimal
```

Expected: clean compile.

- [ ] **Step 4: Commit**

```bash
git add engine/include/mforce/music/harmony_timeline.h engine/include/mforce/music/structure.h
git commit -m "feat(harmony): HarmonyTimeline for per-beat chord lookup"
```

---

### Task 5: StrategyScope + Locus Harmony Access

**Files:**
- Modify: `engine/include/mforce/music/strategy.h`
- Modify: `engine/include/mforce/music/locus.h`

- [ ] **Step 1: Add StrategyScope to strategy.h**

In `engine/include/mforce/music/strategy.h`, add the enum before `class FigureStrategy`:

```cpp
enum class StrategyScope { Melody, MelodyAndHarmony };
```

Add `scope()` virtual to `PassageStrategy`:

```cpp
class PassageStrategy {
public:
  virtual ~PassageStrategy() = default;
  virtual std::string name() const = 0;
  virtual StrategyScope scope() const { return StrategyScope::Melody; }

  virtual PassageTemplate plan_passage(Locus /*locus*/, PassageTemplate seed) {
    return seed;
  }
  virtual Passage compose_passage(Locus, const PassageTemplate&) = 0;
};
```

- [ ] **Step 2: Add harmony pointer to Locus**

In `engine/include/mforce/music/locus.h`, add a forward-declare before `struct Locus`:

```cpp
struct HarmonyTimeline;
```

Add to the Locus struct after `int figureIdx{-1};`:

```cpp
  const HarmonyTimeline* harmony{nullptr};
```

- [ ] **Step 3: Build to verify**

Run:
```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/tools/mforce_cli/mforce_cli.vcxproj -p:Configuration=Release -nologo -v:minimal
```

Expected: clean compile. Existing strategies see `scope()` defaulting to `Melody`.

- [ ] **Step 4: Commit**

```bash
git add engine/include/mforce/music/strategy.h engine/include/mforce/music/locus.h
git commit -m "feat(harmony): StrategyScope enum + Locus::harmony pointer"
```

---

### Task 6: Wire ChordWalker into Composer

**Files:**
- Modify: `engine/include/mforce/music/templates.h` (add `styleName` to SectionDef)
- Modify: `engine/include/mforce/music/templates_json.h` (JSON for `styleName`)
- Modify: `engine/include/mforce/music/composer.h` (harmony pipeline)

This is the core wiring: Composer's `setup_piece_` builds HarmonyTimeline for each Section using ChordWalker, respecting strategy scope. The compose loop provides harmony via Locus.

- [ ] **Step 1: Add styleName to SectionDef**

In `engine/include/mforce/music/templates.h`, add to `struct SectionDef` after `std::vector<KeyContext> keyContexts;`:

```cpp
        std::string styleName;  // style table name for ChordWalker (empty = use progressionName or inline)
```

- [ ] **Step 2: Add JSON support for styleName**

In `engine/include/mforce/music/templates_json.h`, in the `from_json` for `SectionDef`, after the `keyContexts` block:

```cpp
    if (j.contains("styleName")) {
        sd.styleName = j["styleName"].get<std::string>();
    }
```

In the `to_json` for `SectionDef` (if it exists), add:
```cpp
    if (!sd.styleName.empty()) j["styleName"] = sd.styleName;
```

- [ ] **Step 3: Update Composer's setup_piece_ to build HarmonyTimeline**

In `engine/include/mforce/music/composer.h`, add includes at the top (after existing includes):

```cpp
#include "mforce/music/chord_walker.h"
#include "mforce/music/harmony_timeline.h"
```

In `setup_piece_`, after the existing harmony wiring block (the `if (sd.chordProgression) ... else if (!sd.progressionName.empty())` block around line 225-229), add:

```cpp
      // Populate HarmonyTimeline from whatever source provided the progression.
      if (section.chordProgression) {
        section.harmonyTimeline.set_segment(
            0.0f, sd.beats, *section.chordProgression, "authored");
      }
```

This ensures that any pre-authored or ChordProgressionBuilder-generated progression immediately populates the timeline. ChordWalker-based generation will be handled in the compose loop (next step).

- [ ] **Step 4: Update compose_passage_ to provide harmony via Locus and respect scope**

In `compose_passage_` (around line 311), modify the Locus construction to include harmony:

Replace the existing Locus creation (around line 343):
```cpp
      Locus locus{&piece, const_cast<PieceTemplate*>(&tmpl), sectionIdx, partIdx};
```

With:
```cpp
      Locus locus{&piece, const_cast<PieceTemplate*>(&tmpl), sectionIdx, partIdx};
      if (section) {
        locus.harmony = &section->harmonyTimeline;
      }
```

Then, before calling `compose_passage`, check strategy scope and ensure harmony exists:

After the Locus construction but before `::mforce::rng::Scope rngScope(rng_);`, add:

```cpp
      // Check strategy scope — if Melody-only, ensure harmony exists for this passage.
      // If MelodyAndHarmony, the strategy will populate harmony itself.
      PassageStrategy* strat = StrategyRegistry::instance().resolve_passage(
          passIt->second.strategy.empty() ? "default_passage" : passIt->second.strategy);
      if (strat && strat->scope() == StrategyScope::Melody
          && section && section->harmonyTimeline.empty()
          && !sd.styleName.empty()) {
        // Generate harmony via ChordWalker for the whole section
        auto style = StyleTable::load_by_name(sd.styleName);
        WalkConstraint wc;
        wc.startChord = ScaleChord{0, 0, &ChordDef::get("Major")}; // I
        wc.totalBeats = sd.beats;
        auto prog = ChordWalker::walk(style, wc, tmpl.masterSeed + sectionIdx * 1000);
        const_cast<Section*>(section)->harmonyTimeline.set_segment(
            0.0f, sd.beats, prog, "chord_walker");
        const_cast<Section*>(section)->chordProgression = prog;
      }
```

Note: `const_cast` here is transitional — same pattern used for realize_motifs_. The Section is conceptually mutable during composition.

Also find `sd` via tmpl: add before the scope-check block:
```cpp
      const PieceTemplate::SectionDef* sd = nullptr;
      for (const auto& s : tmpl.sections) {
        if (s.name == sectionName) { sd = &s; break; }
      }
```

- [ ] **Step 5: Build to verify**

Run:
```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/tools/mforce_cli/mforce_cli.vcxproj -p:Configuration=Release -nologo -v:minimal
```

Expected: clean compile. No behavioral change yet for existing patches (they don't set `styleName`).

- [ ] **Step 6: Verify existing golden renders still match**

Run the K467 period render and compare:
```bash
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/PluckU.json renders/k467_period 1 --template patches/test_k467_period.json
```

Verify output matches previous render (same peak/rms values). The period strategy doesn't use ChordWalker yet, so this should be identical.

- [ ] **Step 7: Commit**

```bash
git add engine/include/mforce/music/templates.h engine/include/mforce/music/templates_json.h engine/include/mforce/music/composer.h
git commit -m "feat(harmony): wire ChordWalker into Composer pipeline"
```

---

### Task 7: PeriodPassageStrategy Harmony Authoring

**Files:**
- Modify: `engine/include/mforce/music/period_passage_strategy.h`

PPS declares `scope() = MelodyAndHarmony` and calls ChordWalker with cadence-derived pins during `compose_passage`.

- [ ] **Step 1: Add scope override and harmony authoring to PPS**

In `engine/include/mforce/music/period_passage_strategy.h`, add include at top:

```cpp
#include "mforce/music/chord_walker.h"
```

Add scope override to the class declaration:

```cpp
class PeriodPassageStrategy : public PassageStrategy {
public:
  std::string name() const override { return "period_passage"; }
  StrategyScope scope() const override { return StrategyScope::MelodyAndHarmony; }

  PassageTemplate plan_passage(Locus locus, PassageTemplate seed) override;
  Passage         compose_passage(Locus locus, const PassageTemplate& pt) override;
};
```

Then, in `compose_passage`, add harmony generation BEFORE the phrase loop (after the PitchReader setup, around line 131). Insert after `runningReader.set_pitch(*pt.startingPitch);`:

```cpp
  // --- Harmony authoring ---
  // If a style table is available via Locus, generate harmony with cadence pins.
  const auto& sec = locus.piece->sections[locus.sectionIdx];
  const PieceTemplate::SectionDef* sd = nullptr;
  for (const auto& s : locus.pieceTemplate->sections) {
    if (s.name == sec.name) { sd = &s; break; }
  }

  // Note: compose_passage receives the PLANNED template (periods already
  // flattened to phrases). But plan_passage preserves the original
  // periods[] on the planned template (it clears phrases and rebuilds,
  // but doesn't clear periods). So pt.periods is still available here.

  if (sd && !sd->styleName.empty() && sec.harmonyTimeline.empty()
      && !pt.periods.empty()) {
    auto style = StyleTable::load_by_name(sd->styleName);
    float beatOffset = 0.0f;

    for (int pi = 0; pi < (int)pt.periods.size(); ++pi) {
      const PeriodSpec& period = pt.periods[pi];
      float barsPerPhrase = period.bars / 2.0f;
      float beatsPerBar = float(sec.meter.beats_per_bar());
      float anteBeats = barsPerPhrase * beatsPerBar;
      float consBeats = barsPerPhrase * beatsPerBar;

      // Antecedent: I → cadence target
      {
        WalkConstraint wc;
        wc.startChord = ScaleChord{0, 0, &ChordDef::get("Major")};
        if (period.antecedent.cadenceTarget >= 0) {
          // Map cadence target (scale degree) to a chord.
          // HC target is typically V (degree 4), but the melody target
          // may be a chord tone, not the root. For now: degree 4 = V chord.
          wc.endChord = cadence_chord(period.antecedent.cadenceType,
                                       period.antecedent.cadenceTarget);
        }
        wc.totalBeats = anteBeats;
        auto prog = ChordWalker::walk(style, wc,
            locus.pieceTemplate->masterSeed + pi * 100);
        const_cast<Section&>(sec).harmonyTimeline.set_segment(
            beatOffset, beatOffset + anteBeats, prog, "period_passage");
        beatOffset += anteBeats;
      }

      // Consequent: I → cadence target
      {
        WalkConstraint wc;
        wc.startChord = ScaleChord{0, 0, &ChordDef::get("Major")};
        if (period.consequent.cadenceTarget >= 0) {
          wc.endChord = cadence_chord(period.consequent.cadenceType,
                                       period.consequent.cadenceTarget);
        }
        wc.totalBeats = consBeats;
        auto prog = ChordWalker::walk(style, wc,
            locus.pieceTemplate->masterSeed + pi * 100 + 50);
        const_cast<Section&>(sec).harmonyTimeline.set_segment(
            beatOffset, beatOffset + consBeats, prog, "period_passage");
        beatOffset += consBeats;
      }
    }
  }
```

Add a private helper at the bottom of the class (before the closing `};`):

```cpp
private:
  // Map cadenceType + target degree to a ScaleChord for the ChordWalker endpoint.
  static ScaleChord cadence_chord(int cadenceType, int targetDegree) {
    // cadenceType 1 = HC (half cadence) → target is typically V
    // cadenceType 2 = PAC/IAC → target is typically I
    // The target degree from the melody side may not be the chord root.
    // For HC: chord is V (degree 4) regardless of melody target.
    // For authentic: chord is I (degree 0).
    if (cadenceType == 1) {
      return ScaleChord{4, 0, &ChordDef::get("Major")};   // V
    } else {
      return ScaleChord{0, 0, &ChordDef::get("Major")};   // I
    }
  }
```

- [ ] **Step 2: Verify Meter::beats_per_bar() exists**

`Meter::beats_per_bar()` already exists at `basics.h:268` and returns `int`. The code in Step 1 uses it via `float(sec.meter.beats_per_bar())` — no changes needed.

- [ ] **Step 3: Build to verify**

Run:
```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/tools/mforce_cli/mforce_cli.vcxproj -p:Configuration=Release -nologo -v:minimal
```

Expected: clean compile.

- [ ] **Step 4: Verify K467 period render is unchanged**

The K467 template doesn't have `styleName` set yet, so PPS should skip harmony generation:

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/PluckU.json renders/k467_period 1 --template patches/test_k467_period.json
```

Verify same output as before (no behavioral change).

- [ ] **Step 5: Commit**

```bash
git add engine/include/mforce/music/period_passage_strategy.h
git commit -m "feat(harmony): PPS authors harmony via ChordWalker with cadence pins"
```

---

### Task 8: Smoke Test — K467 with ChordWalker Harmony

**Files:**
- Create: `patches/test_k467_walker.json`

Copy `patches/test_k467_period.json` and add `styleName` to the section. This enables PPS's harmony authoring path. The melody render should be identical (it doesn't use chord context yet); the new data is the HarmonyTimeline on the Section which we can verify by inspecting the JSON output.

- [ ] **Step 1: Create test_k467_walker.json**

Copy `patches/test_k467_period.json` to `patches/test_k467_walker.json`. Then modify the section to add `styleName`:

In the `"sections"` array, change:
```json
{"name": "Main", "beats": 48}
```
To:
```json
{"name": "Main", "beats": 48, "styleName": "classical_mozart"}
```

- [ ] **Step 2: Render and verify**

```bash
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/PluckU.json renders/k467_walker 1 --template patches/test_k467_walker.json
```

Expected: renders successfully. Peak/rms should be similar to the period render (melody unchanged; harmony is data-only, not yet performed).

Inspect the rendered JSON (`renders/k467_walker_1.json`) to verify melody phrases are unchanged.

- [ ] **Step 3: Verify HarmonyTimeline was populated**

The HarmonyTimeline is on the Section (runtime struct), not serialized to JSON yet. To verify it was populated, add a temporary print in PPS's harmony block or inspect via debugger. Alternatively, add a simple JSON dump of the timeline to the compose output.

For a quick verification, add to `run_compose` in `tools/mforce_cli/main.cpp`, after the piece JSON is saved, a chord summary print:

```cpp
// After "Saved: jsonPath" line
for (const auto& sec : piece.sections) {
  if (!sec.harmonyTimeline.empty()) {
    std::cout << "  Harmony for '" << sec.name << "':";
    for (const auto& seg : sec.harmonyTimeline.segments) {
      std::cout << " [" << seg.startBeat << "-" << seg.endBeat << ": "
                << seg.progression.count() << " chords]";
    }
    std::cout << "\n";
  }
}
```

Re-render and verify output shows harmony segments for each period's antecedent and consequent.

- [ ] **Step 4: Commit**

```bash
git add patches/test_k467_walker.json tools/mforce_cli/main.cpp
git commit -m "test(harmony): K467 smoke test with ChordWalker-generated harmony"
```

---

### Task 9: Remove Temporary Includes + Cleanup

**Files:**
- Modify: `engine/include/mforce/music/templates_json.h` (remove temporary includes from Task 1/3)
- Verify: all includes are wired through proper dependency chains

- [ ] **Step 1: Remove temporary includes**

Remove the temporary `#include "mforce/music/style_table.h"` and `#include "mforce/music/chord_walker.h"` from `templates_json.h` (added in Tasks 1 and 3 for compile verification). These are now included via `composer.h` and `period_passage_strategy.h`.

- [ ] **Step 2: Final build + render verification**

```bash
"C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/MSBuild.exe" build/tools/mforce_cli/mforce_cli.vcxproj -p:Configuration=Release -nologo -v:minimal
```

Run both renders:
```bash
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/PluckU.json renders/k467_period 1 --template patches/test_k467_period.json
build/tools/mforce_cli/Release/mforce_cli.exe --compose patches/PluckU.json renders/k467_walker 1 --template patches/test_k467_walker.json
```

Verify:
- k467_period render is byte-identical to pre-plan baseline (no `styleName`, no ChordWalker involvement)
- k467_walker render produces melody + harmony timeline output

- [ ] **Step 3: Commit**

```bash
git add engine/include/mforce/music/templates_json.h
git commit -m "chore: remove temporary includes from compile verification"
```
