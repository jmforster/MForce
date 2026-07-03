# Constrained Atom Supply — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Python-generated, tagged-and-weighted pool of Markov figure atoms, plus a C++ `PoolFigureBuilder` that selects a constraint-matching atom (`noteCount` / `totalBeats` / `contour`), so the strategy layer can request idiomatic atoms by spec.

**Architecture:** Two halves joined by a pool JSON contract. Python (`markov_pool.py`) samples the Phase-1 Markov model, leap-caps, tags each atom with measured length/beats/contour and a generative `count` (weight), and writes `lib/figures/markov_pool.json`. C++ `PoolFigureBuilder` (sibling to `RandomFigureBuilder`, same `build(Constraints)`) loads the pool, filters by present constraints, and weighted-selects by `count`.

**Tech Stack:** Python 3.11 stdlib. C++17 header-only engine. Existing `tools/test_figures` assert harness.

## Global Constraints

- **Python:** 3.11, stdlib only, no pytest. Tests are plain `assert` scripts (`python test_*.py` → prints `OK`, exit 0).
- **C++ tests:** added to `tools/test_figures/main.cpp` using its `EXPECT_EQ` / `EXPECT_NEAR` / `RUN_TEST` macros. Build: `cmake --build build --target test_figures --config Debug` (from repo root). Run: `build/tools/test_figures/Debug/test_figures.exe` (exit 0 = pass).
- **Engine is header-only** in this area — `PoolFigureBuilder` is a header (`pool_figure_builder.h`), like `random_figure_builder.h`.
- **Constraints reuse:** `noteCount` = existing `Constraints.count`; `totalBeats` = existing `Constraints.length`. Only `contour` is new.
- **Contour classes:** `Up, Down, Arch, Valley, Level`. Classifier uses cumulative scale-degree path; `R = 2` (min return for Arch/Valley), tunable. `Level` if `hi-lo<=1 and |e|<=1`.
- **Leap cap:** exclude any atom with `abs(step) > 7` (one octave).
- **Pool JSON path:** `lib/figures/markov_pool.json`. Schema: `{version, source, leapCapDegrees, atoms:[{units:[{duration,step}], noteCount, totalBeats, contour, count}]}`.
- **Selection:** filter by whichever of `count`/`length`/`contour` are present (absent = wildcard; `length` matches within tolerance 0.01 beats); weighted-select among matches by `count`; **no match → throw `std::runtime_error`** (parallel to `RandomFigureBuilder`).
- **Reproducibility:** seed all RNGs; pool generation takes `--seed`.
- **Commits:** repo commits only when Matt asks (shared tree). Treat "Commit" steps as checkpoints to request.

## File Structure

- Create: `corpus/mtd_seg/markov_pool.py` — classifier + sampler + leap-cap + tag + weighted dedupe + emit.
- Create: `corpus/mtd_seg/test_markov_pool.py` — Python asserts.
- Create: `lib/figures/markov_pool.json` — generated artifact.
- Modify: `engine/include/mforce/music/figure_constraints.h` — `Contour` enum + `contour` field + string helpers.
- Create: `engine/include/mforce/music/pool_figure_builder.h` — the selector.
- Modify: `tools/test_figures/main.cpp` — selector tests.

---

### Task 1: Contour classifier (Python)

**Files:**
- Create: `corpus/mtd_seg/markov_pool.py`
- Test: `corpus/mtd_seg/test_markov_pool.py`

**Interfaces:**
- Produces: `classify_contour(steps: list[int], R: int = 2) -> str` returning one of
  `"Up"|"Down"|"Arch"|"Valley"|"Level"`. `steps` includes the leading anchor `0`.

- [ ] **Step 1: Write the failing test**

```python
# test_markov_pool.py
from markov_pool import classify_contour

def test_contour_cases():
    assert classify_contour([0,1,1,1])      == "Up"      # C-D-E-F
    assert classify_contour([0,-1,-1,-1])   == "Down"    # C-B-A-G
    assert classify_contour([0,1,1,-1,-1])  == "Arch"    # C-D-E-D-C
    assert classify_contour([0,-1,-1,1,1])  == "Valley"  # C-B-A-B-C
    assert classify_contour([0,1,-1,1])     == "Level"   # C-D-C-D hover
    assert classify_contour([0,0,0])        == "Level"   # repeated
    assert classify_contour([0,1,1,1,-1])   == "Up"      # C-D-E-F-E (R=2: pullback<2)
    assert classify_contour([0,4,-4])       == "Arch"    # C-G-C

if __name__ == "__main__":
    test_contour_cases()
    print("OK")
```

- [ ] **Step 2: Run test to verify it fails**

Run: `cd corpus/mtd_seg && python test_markov_pool.py`
Expected: FAIL — `ImportError: cannot import name 'classify_contour'`

- [ ] **Step 3: Write minimal implementation**

```python
# markov_pool.py
"""Tagged, weighted Markov atom pool for the constrained-atom supply layer."""

def classify_contour(steps, R=2):
    d, c = [], 0
    for s in steps:
        c += s; d.append(c)
    e, hi, lo = d[-1], max(d), min(d)
    upExc   = hi - max(0, e)
    downExc = min(0, e) - lo
    if hi - lo <= 1 and abs(e) <= 1:
        return "Level"
    if upExc >= R and upExc >= downExc:
        return "Arch"
    if downExc >= R and downExc > upExc:
        return "Valley"
    if e > 0:
        return "Up"
    if e < 0:
        return "Down"
    return "Level"
```

- [ ] **Step 4: Run test to verify it passes**

Run: `cd corpus/mtd_seg && python test_markov_pool.py`
Expected: prints `OK`.

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add corpus/mtd_seg/markov_pool.py corpus/mtd_seg/test_markov_pool.py
git -C /c/@dev/repos/mforce commit -m "feat(pool): contour classifier (5-class, R=2)"
```

---

### Task 2: Pool builder — leap-cap, tag, weighted dedupe, emit (Python)

**Files:**
- Modify: `corpus/mtd_seg/markov_pool.py`
- Test: `corpus/mtd_seg/test_markov_pool.py`
- Create (generated): `lib/figures/markov_pool.json`

**Interfaces:**
- Consumes: `classify_contour` (Task 1); `MarkovModel.load` + `gen_model` (Phase 1).
- Produces:
  - `LEAP_CAP = 7`; `within_leap_cap(steps) -> bool`.
  - `tag_atom(steps, pulses) -> dict` with keys `units, noteCount, totalBeats, contour, count`.
  - `accumulate(pool: dict, steps, pulses) -> None` (dedupe by content, bump `count`).
  - `main()` — `python markov_pool.py [--per-k N] [--seed S]` writes the pool JSON + coverage report.

- [ ] **Step 1: Write the failing test**

```python
# append to test_markov_pool.py
from markov_pool import within_leap_cap, tag_atom, accumulate

def test_leap_cap():
    assert within_leap_cap([0,1,7,-7]) is True
    assert within_leap_cap([0,8]) is False        # 8 > one octave

def test_tag_atom():
    a = tag_atom([0,1,1], [0.5,0.5,0.5])
    assert a["noteCount"] == 3
    assert a["totalBeats"] == 1.5
    assert a["contour"] == "Up"
    assert a["count"] == 1
    assert a["units"] == [{"duration":0.5,"step":0},{"duration":0.5,"step":1},{"duration":0.5,"step":1}]

def test_accumulate_weights():
    pool = {}
    accumulate(pool, [0,1], [0.5,0.5])
    accumulate(pool, [0,1], [0.5,0.5])     # same content
    accumulate(pool, [0,-1], [0.5,0.5])    # different
    counts = sorted(a["count"] for a in pool.values())
    assert counts == [1, 2]
```
(Add the three test calls to the `__main__` block.)

- [ ] **Step 2: Run test to verify it fails**

Run: `cd corpus/mtd_seg && python test_markov_pool.py`
Expected: FAIL — `ImportError: cannot import name 'within_leap_cap'`

- [ ] **Step 3: Write minimal implementation**

```python
# markov_pool.py  (append)
import argparse, json, pathlib, random
from collections import Counter

REPO = pathlib.Path(__file__).resolve().parent.parent.parent
OUT  = REPO / "lib" / "figures" / "markov_pool.json"
LEAP_CAP = 7

def within_leap_cap(steps):
    return all(abs(s) <= LEAP_CAP for s in steps)

def tag_atom(steps, pulses):
    return {
        "units": [{"duration": float(pulses[i]), "step": int(steps[i])}
                  for i in range(len(steps))],
        "noteCount": len(steps),
        "totalBeats": round(sum(pulses), 6),
        "contour": classify_contour(steps),
        "count": 1,
    }

def _key(steps, pulses):
    return (tuple(int(s) for s in steps), tuple(round(float(p), 6) for p in pulses))

def accumulate(pool, steps, pulses):
    k = _key(steps, pulses)
    if k in pool:
        pool[k]["count"] += 1
    else:
        pool[k] = tag_atom(steps, pulses)

def generate_pool(model, rng, per_k=5000, kmin=2, kmax=9):
    from markov_generate import gen_model
    pool = {}
    for k in range(kmin, kmax + 1):
        for _ in range(per_k):
            step, pulse = gen_model(model, k, rng)
            if within_leap_cap(step):
                accumulate(pool, step, pulse)
    return list(pool.values())

def main():
    from markov_model import MarkovModel
    ap = argparse.ArgumentParser()
    ap.add_argument("--per-k", type=int, default=5000)
    ap.add_argument("--seed", type=int, default=1234)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    model = MarkovModel.load()
    atoms = generate_pool(model, rng, per_k=args.per_k)

    OUT.parent.mkdir(parents=True, exist_ok=True)
    OUT.write_text(json.dumps({
        "version": 1, "source": "mtd_markov_order2", "leapCapDegrees": LEAP_CAP,
        "atoms": atoms,
    }), encoding="utf-8")

    cov = Counter((a["noteCount"], a["contour"]) for a in atoms)
    print(f"wrote {OUT.relative_to(REPO)}  ({len(atoms)} unique atoms)")
    print("coverage (noteCount x contour) — unique atoms:")
    for k in range(2, 10):
        row = {c: cov.get((k, c), 0) for c in ["Up","Down","Arch","Valley","Level"]}
        empties = [c for c, n in row.items() if n == 0]
        print(f"  k={k}: {row}" + (f"  EMPTY: {empties}" if empties else ""))

if __name__ == "__main__":
    main()
```

- [ ] **Step 4: Run unit tests, then generate the pool**

Run: `cd corpus/mtd_seg && python test_markov_pool.py`
Expected: prints `OK`.
Run: `cd corpus/mtd_seg && python markov_pool.py`
Expected: `wrote lib/figures/markov_pool.json (<N> unique atoms)` + a coverage table. Note any
`EMPTY` buckets (expected for odd combos like high-k Level) — these are the corpus-shaped gaps the
selector handles via no-match, not a bug.

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add corpus/mtd_seg/markov_pool.py corpus/mtd_seg/test_markov_pool.py lib/figures/markov_pool.json
git -C /c/@dev/repos/mforce commit -m "feat(pool): generate tagged+weighted Markov atom pool"
```

---

### Task 3: C++ Contour enum + Constraints field + string helpers

**Files:**
- Modify: `engine/include/mforce/music/figure_constraints.h`
- Modify: `tools/test_figures/main.cpp`

**Interfaces:**
- Produces: `enum class Contour { Up, Down, Arch, Valley, Level };`,
  `std::optional<Contour> Constraints::contour`,
  `const char* to_string(Contour)`, `Contour contour_from_string(const std::string&)` (throws on
  unknown).

- [ ] **Step 1: Write the failing test** (in `tools/test_figures/main.cpp`)

Add this test function near the other tests, and a `RUN_TEST(test_contour_roundtrip);` line in `main`:

```cpp
static int test_contour_roundtrip() {
    using mforce::Contour;
    EXPECT_EQ(std::string(mforce::to_string(Contour::Arch)), std::string("Arch"), "to_string Arch");
    EXPECT_EQ(int(mforce::contour_from_string("Valley")), int(Contour::Valley), "from_string Valley");
    EXPECT_EQ(int(mforce::contour_from_string("Up")),     int(Contour::Up),     "from_string Up");
    mforce::Constraints c;
    c.contour = Contour::Level;
    EXPECT_EQ(bool(c.contour), true, "contour field settable");
    return 0;
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build build --target test_figures --config Debug`
Expected: FAIL — compile error, `Contour` / `to_string` / `contour_from_string` undeclared.

- [ ] **Step 3: Write minimal implementation** (`figure_constraints.h`)

Add above `struct Constraints`:

```cpp
#include <string>
#include <stdexcept>

enum class Contour { Up, Down, Arch, Valley, Level };

inline const char* to_string(Contour c) {
    switch (c) {
        case Contour::Up:     return "Up";
        case Contour::Down:   return "Down";
        case Contour::Arch:   return "Arch";
        case Contour::Valley: return "Valley";
        case Contour::Level:  return "Level";
    }
    return "Up";
}

inline Contour contour_from_string(const std::string& s) {
    if (s == "Up")     return Contour::Up;
    if (s == "Down")   return Contour::Down;
    if (s == "Arch")   return Contour::Arch;
    if (s == "Valley") return Contour::Valley;
    if (s == "Level")  return Contour::Level;
    throw std::runtime_error("contour_from_string: unknown contour '" + s + "'");
}
```

Add to `struct Constraints` (after `maxPulse`):

```cpp
  std::optional<Contour> contour;      // target melodic contour (pool selection)
```

- [ ] **Step 4: Build and run to verify it passes**

Run: `cmake --build build --target test_figures --config Debug`
Then: `build/tools/test_figures/Debug/test_figures.exe`
Expected: `[TEST] test_contour_roundtrip ... PASS`, exe exit 0.

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add engine/include/mforce/music/figure_constraints.h tools/test_figures/main.cpp
git -C /c/@dev/repos/mforce commit -m "feat(constraints): Contour enum + contour field + string helpers"
```

---

### Task 4: PoolFigureBuilder — load, filter, weighted-select, no-match throw (C++)

**Files:**
- Create: `engine/include/mforce/music/pool_figure_builder.h`
- Modify: `tools/test_figures/main.cpp`

**Interfaces:**
- Consumes: `Constraints` + `Contour` (Task 3); `MelodicFigure` (`figures.h`); `Randomizer`
  (`core/randomizer.h`); `nlohmann::json`.
- Produces:
  - `struct PoolAtom { MelodicFigure figure; int noteCount; float totalBeats; Contour contour; long count; };`
  - `class PoolFigureBuilder` with:
    - `PoolFigureBuilder(const nlohmann::json& poolJson, uint32_t seed);`
    - `static PoolFigureBuilder load(const std::string& path, uint32_t seed);`
    - `MelodicFigure build(const Constraints& c);` — filter present constraints, weighted-select by
      `count`, throw `std::runtime_error` on no match.

- [ ] **Step 1: Write the failing test** (in `tools/test_figures/main.cpp`)

Add this test and a `RUN_TEST(test_pool_select);` line in `main`:

```cpp
static int test_pool_select() {
    using namespace mforce;
    nlohmann::json pool = {
        {"version", 1}, {"source", "fixture"}, {"leapCapDegrees", 7},
        {"atoms", {
            // 3-note Up, heavy weight
            {{"units", {{{"duration",0.5},{"step",0}},{{"duration",0.5},{"step",1}},{{"duration",0.5},{"step",1}}}},
             {"noteCount",3},{"totalBeats",1.5},{"contour","Up"},{"count",100}},
            // 3-note Up, light weight (distinguishable: ends with a leap)
            {{"units", {{{"duration",0.5},{"step",0}},{{"duration",0.5},{"step",1}},{{"duration",0.5},{"step",4}}}},
             {"noteCount",3},{"totalBeats",1.5},{"contour","Up"},{"count",1}},
            // 3-note Down
            {{"units", {{{"duration",0.5},{"step",0}},{{"duration",0.5},{"step",-1}},{{"duration",0.5},{"step",-1}}}},
             {"noteCount",3},{"totalBeats",1.5},{"contour","Down"},{"count",50}},
        }}
    };
    PoolFigureBuilder b(pool, 12345u);

    // (a) contour filter: requesting Down never returns an Up atom (net_step > 0).
    for (int i = 0; i < 50; ++i) {
        Constraints c; c.contour = Contour::Down;
        MelodicFigure f = b.build(c);
        EXPECT_EQ(f.net_step() < 0, true, "Down request returns descending atom");
    }

    // (b) weighting: among 'Up', the count=100 atom (net +2) dominates the count=1 (net +5).
    int heavy = 0, light = 0;
    for (int i = 0; i < 2000; ++i) {
        Constraints c; c.contour = Contour::Up;
        MelodicFigure f = b.build(c);
        if (f.net_step() == 2) ++heavy; else if (f.net_step() == 5) ++light;
    }
    EXPECT_EQ(heavy > light * 5, true, "heavy-count atom dominates light");

    // (c) no match throws.
    bool threw = false;
    try { Constraints c; c.count = 2; c.contour = Contour::Arch; b.build(c); }
    catch (const std::runtime_error&) { threw = true; }
    EXPECT_EQ(threw, true, "over-constrained request throws");
    return 0;
}
```

- [ ] **Step 2: Build to verify it fails**

Run: `cmake --build build --target test_figures --config Debug`
Expected: FAIL — `pool_figure_builder.h` not found / `PoolFigureBuilder` undeclared.

- [ ] **Step 3: Write minimal implementation** (`pool_figure_builder.h`)

```cpp
#pragma once
#include "mforce/music/figures.h"
#include "mforce/music/figure_constraints.h"
#include "mforce/core/randomizer.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <vector>
#include <cmath>
#include <stdexcept>

namespace mforce {

struct PoolAtom {
    MelodicFigure figure;
    int   noteCount;
    float totalBeats;
    Contour contour;
    long  count;
};

class PoolFigureBuilder {
public:
    PoolFigureBuilder(const nlohmann::json& poolJson, uint32_t seed) : rng_(seed) {
        for (const auto& aj : poolJson.at("atoms")) {
            PoolAtom a;
            for (const auto& uj : aj.at("units")) {
                a.figure.units.push_back(FigureUnit{
                    uj.at("duration").get<float>(), uj.at("step").get<int>()});
            }
            a.noteCount  = aj.at("noteCount").get<int>();
            a.totalBeats = aj.at("totalBeats").get<float>();
            a.contour    = contour_from_string(aj.at("contour").get<std::string>());
            a.count      = aj.at("count").get<long>();
            atoms_.push_back(std::move(a));
        }
    }

    static PoolFigureBuilder load(const std::string& path, uint32_t seed) {
        std::ifstream f(path);
        if (!f) throw std::runtime_error("PoolFigureBuilder::load: cannot open " + path);
        nlohmann::json j; f >> j;
        return PoolFigureBuilder(j, seed);
    }

    MelodicFigure build(const Constraints& c) {
        std::vector<const PoolAtom*> matches;
        long total = 0;
        for (const auto& a : atoms_) {
            if (c.count   && a.noteCount != *c.count) continue;
            if (c.length  && std::fabs(a.totalBeats - *c.length) > 0.01f) continue;
            if (c.contour && a.contour != *c.contour) continue;
            matches.push_back(&a);
            total += a.count;
        }
        if (matches.empty())
            throw std::runtime_error("PoolFigureBuilder::build: no atom matches constraints");

        long r = long(rng_.int_range(0, int(total - 1)));
        long acc = 0;
        for (const auto* a : matches) {
            acc += a->count;
            if (r < acc) return a->figure.clone();
        }
        return matches.back()->figure.clone();
    }

private:
    std::vector<PoolAtom> atoms_;
    Randomizer rng_;
};

} // namespace mforce
```

Also add `#include "mforce/music/pool_figure_builder.h"` to the includes at the top of
`tools/test_figures/main.cpp`.

- [ ] **Step 4: Build and run to verify it passes**

Run: `cmake --build build --target test_figures --config Debug`
Then: `build/tools/test_figures/Debug/test_figures.exe`
Expected: `[TEST] test_pool_select ... PASS`, exe exit 0.

- [ ] **Step 5: Smoke-load the real pool, then Commit (checkpoint — ask Matt)**

Add a temporary smoke (or run via a scratch): confirm `PoolFigureBuilder::load("lib/figures/markov_pool.json", 1)` then `build({})` returns a figure with ≥2 units. (Can be a throwaway `RUN_TEST` removed before commit, or a manual check.)

```bash
git -C /c/@dev/repos/mforce add engine/include/mforce/music/pool_figure_builder.h tools/test_figures/main.cpp
git -C /c/@dev/repos/mforce commit -m "feat(pool): PoolFigureBuilder — constraint filter + weighted select"
```

---

## Notes for the implementer

- **`Randomizer::int_range(lo,hi)`** is inclusive `[lo,hi]` per existing usage in
  `random_figure_builder.h` (`countRng.int_range(4, 8)`). `total` fits `int` for realistic pools
  (≤ per_k × 8 samples). If a pool ever exceeds `INT_MAX` weight, widen the draw — not a v1 concern.
- **`MelodicFigure::clone()`** exists (`figures.h`) — return clones so callers can mutate freely.
- **No-match is a feature, not a failure.** The throw is how a strategy learns to relax a
  constraint. Do not "fix" it by returning a nearest atom — that hides the gap.
- **If the contour test mis-tags** (Task 1) once you eyeball real pool atoms, tune `R` / the Level
  band in `classify_contour` — the 5 classes are fixed, the cutoffs are not (per spec).
- **Engine mid-refactor caveat:** composition-tier headers have uncommitted edits (other Claude). If
  `cmake --build` fails on unrelated composer/template errors, that's not this task — surface it.
```
