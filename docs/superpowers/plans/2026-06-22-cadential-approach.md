# Cadential Approach (apply_cadence v2) — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace `apply_cadence`'s single-note pitch yank with a head-preserving reshape: keep the figure's head, rewrite the last ~3 notes into a distance-aware melodic approach that lands on the cadence target, and elide busy tail notes so the final tonic lands long.

**Architecture:** Two pure static helpers on `DefaultPhraseStrategy` (`build_approach_steps`, `settle_tail`) carry the tricky geometry/rhythm and are unit-tested in `tools/test_figures`. `apply_cadence` is rewritten to call them. End-to-end behavior is verified by rendering the period sweep and analyzing the event JSON.

**Tech Stack:** C++17 header-only engine. `tools/test_figures` assert harness. Python for integration analysis.

## Global Constraints

- **Header-only:** all code in `engine/include/mforce/music/default_strategies.h`. Helpers are `static` methods of `DefaultPhraseStrategy` (callable from tests, like the existing `degree_in_scale`).
- **C++ tests:** in `tools/test_figures/main.cpp` (`EXPECT_EQ`/`RUN_TEST`). Build: `& "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build C:\@dev\repos\mforce\build --target test_figures --config Debug` (cmake is NOT on PATH). Run: `build/tools/test_figures/Debug/test_figures.exe` (exit 0 = pass).
- **Integration:** `mforce_cli` target; render `patches/period_markov_test.json`, analyze `renders/*_N.json` with Python.
- **Scale-degree conventions:** degrees are scale-step indices; `scale.length()` = 7 for major. cadenceTarget is a 0-based scale degree (0=tonic, 4=dominant). Compare pitches RELATIVE.
- **Head/tail rule:** `head = max(0, N-3)`; tail = the last `min(3, N)` notes.
- **Settle default:** `finalMin = 1.0` beat (the final tonic should be ≥ this when achievable). Tunable.
- **Total figure duration is preserved** (elision redistributes within the tail) so bars stay whole.
- **Commits:** repo commits only when Matt asks. Treat commit steps as checkpoints.

## File Structure

- Modify: `engine/include/mforce/music/default_strategies.h` — add the two static helpers (declare in the class near `degree_in_scale`; define inline below), rewrite `apply_cadence` body.
- Modify: `tools/test_figures/main.cpp` — unit tests for the helpers.

---

### Task 1: `build_approach_steps` — distance-aware approach geometry

**Files:**
- Modify: `engine/include/mforce/music/default_strategies.h`
- Test: `tools/test_figures/main.cpp`

**Interfaces:**
- Produces (static on `DefaultPhraseStrategy`):
  - `static int nearest_degree_index(int fromIdx, int targetDeg, int len);` — the absolute scale-step index of degree `targetDeg` nearest to `fromIdx`.
  - `static std::vector<int> build_approach_steps(int headEndDeg, int targetDeg, int tailCount, int len);` — `tailCount` step deltas that walk from `headEndDeg` and land the final note on `nearest_degree_index(headEndDeg, targetDeg, len)`.

- [ ] **Step 1: Write the failing test** (in `main.cpp`, add `RUN_TEST(test_build_approach_steps);` to `run_unit_tests`)

```cpp
static int test_build_approach_steps() {
    using D = mforce::DefaultPhraseStrategy;
    // helper: apply steps from a start index, return the landing index
    auto land = [](int start, const std::vector<int>& s){ int p=start; for(int x:s)p+=x; return p; };

    // nearest degree
    EXPECT_EQ(D::nearest_degree_index(10, 0, 7), 7,  "nearest tonic to 10 is 7");
    EXPECT_EQ(D::nearest_degree_index(10, 4, 7), 11, "nearest V to 10 is 11");

    // close descent: headEnd=2 (mi), target tonic(0) -> stepwise down, lands on 0, penult=1
    auto a = D::build_approach_steps(2, 0, 3, 7);
    EXPECT_EQ(int(a.size()), 3, "tailCount honored");
    EXPECT_EQ(land(2, a), 0, "lands on tonic");
    EXPECT_EQ(2 + a[0] + a[1], 1, "penultimate is degree 1 (re/2)");

    // ascending from below: headEnd=-2, target tonic(0) -> step up, penult=-1 (ti/7)
    auto b = D::build_approach_steps(-2, 0, 3, 7);
    EXPECT_EQ(land(-2, b), 0, "ascends to tonic");
    EXPECT_EQ(-2 + b[0] + b[1], -1, "penultimate is degree -1 (ti/7)");

    // far above: headEnd=5, target tonic(0), 3 notes -> first note leaps, then steps, lands on 0
    auto c = D::build_approach_steps(5, 0, 3, 7);
    EXPECT_EQ(land(5, c), 0, "far: lands on tonic");
    EXPECT_EQ(c[1], -1, "far: penultimate move is a step");
    EXPECT_EQ(c[2], -1, "far: final move is a step");

    // on target: headEnd=0, target 0, 3 notes -> neighbor escape, last two = -1,+1
    auto e = D::build_approach_steps(0, 0, 3, 7);
    EXPECT_EQ(land(0, e), 0, "on-target lands on target");
    EXPECT_EQ(e[1], -1, "on-target penult steps to LT");
    EXPECT_EQ(e[2], 1,  "on-target resolves up");
    return 0;
}
```

- [ ] **Step 2: Build to verify it fails**

Run the cmake build command (Global Constraints). Expected: compile error — `build_approach_steps` / `nearest_degree_index` undeclared.

- [ ] **Step 3: Write minimal implementation** (in `default_strategies.h`)

Declare inside `class DefaultPhraseStrategy` near `degree_in_scale`:

```cpp
  static int nearest_degree_index(int fromIdx, int targetDeg, int len);
  static std::vector<int> build_approach_steps(int headEndDeg, int targetDeg,
                                               int tailCount, int len);
```

Define inline below the class (near the other inline definitions):

```cpp
inline int DefaultPhraseStrategy::nearest_degree_index(int fromIdx, int targetDeg, int len) {
    int base = fromIdx - (((fromIdx % len) + len) % len);  // octave floor at/below fromIdx
    int best = base + targetDeg;
    for (int cand : {base + targetDeg - len, base + targetDeg, base + targetDeg + len}) {
        if (std::abs(cand - fromIdx) < std::abs(best - fromIdx)) best = cand;
    }
    return best;
}

inline std::vector<int> DefaultPhraseStrategy::build_approach_steps(
    int headEndDeg, int targetDeg, int tailCount, int len) {
    std::vector<int> steps(std::max(0, tailCount), 0);
    if (tailCount <= 0) return steps;

    int targetAbs = nearest_degree_index(headEndDeg, targetDeg, len);
    int delta = targetAbs - headEndDeg;

    if (delta == 0) {                       // on target: lower-neighbor escape -> resolve
        if (tailCount >= 2) {
            steps[tailCount - 2] = -1;       // down to leading-tone/neighbor
            steps[tailCount - 1] = +1;       // resolve up to target
        }                                    // tailCount==1: hold (step 0)
        return steps;
    }

    int dir  = (delta > 0) ? +1 : -1;
    int dist = std::abs(delta);
    if (dist <= tailCount) {                 // stepwise: single steps packed at the end
        for (int i = 0; i < dist; ++i) steps[tailCount - dist + i] = dir;
    } else {                                 // far: leap on first note, then step in
        int stepPortion = tailCount - 1;
        steps[0] = delta - dir * stepPortion;
        for (int i = 1; i < tailCount; ++i) steps[i] = dir;
    }
    return steps;
}
```

- [ ] **Step 4: Build and run to verify it passes**

Build (cmake command), then run the exe. Expected: `[TEST] test_build_approach_steps ... PASS`, exit 0.

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add engine/include/mforce/music/default_strategies.h tools/test_figures/main.cpp
git -C /c/@dev/repos/mforce commit -m "feat(cadence): distance-aware approach-step geometry"
```

---

### Task 2: `settle_tail` — elide busy notes into a longer final

**Files:**
- Modify: `engine/include/mforce/music/default_strategies.h`
- Test: `tools/test_figures/main.cpp`

**Interfaces:**
- Produces: `static std::vector<float> settle_tail(const std::vector<float>& tailDurs, float finalMin);`
  — returns the new tail durations: the largest note count whose final note is `>= finalMin`
  (eliding from the end into the final). Sum is preserved. Returned size is the approach note
  count `A` (≤ `tailDurs.size()`, ≥ 1).

- [ ] **Step 1: Write the failing test** (add `RUN_TEST(test_settle_tail);`)

```cpp
static int test_settle_tail() {
    using D = mforce::DefaultPhraseStrategy;
    auto sum = [](const std::vector<float>& v){ float t=0; for(float x:v)t+=x; return t; };

    // not busy: last note already >= finalMin -> unchanged (3 notes)
    auto a = D::settle_tail({0.5f, 0.5f, 1.0f}, 1.0f);
    EXPECT_EQ(int(a.size()), 3, "no elision when final long enough");
    EXPECT_NEAR(a.back(), 1.0f, 1e-4, "final unchanged");

    // busy: [0.25,0.25,0.25,0.25] D=1.0, finalMin=1.0 -> collapse to 1 note of 1.0
    auto b = D::settle_tail({0.25f,0.25f,0.25f,0.25f}, 1.0f);
    EXPECT_EQ(int(b.size()), 1, "fully collapsed");
    EXPECT_NEAR(b.back(), 1.0f, 1e-4, "final absorbs all");

    // partial: [0.5,0.25,0.25] D=1.0, finalMin=0.5 -> A=2: [0.5, 0.5]
    auto c = D::settle_tail({0.5f,0.25f,0.25f}, 0.5f);
    EXPECT_EQ(int(c.size()), 2, "elide one");
    EXPECT_NEAR(c[0], 0.5f, 1e-4, "lead kept");
    EXPECT_NEAR(c[1], 0.5f, 1e-4, "final absorbs the rest");

    // sum preserved in all cases
    EXPECT_NEAR(sum(a), 2.0f, 1e-4, "sum a");
    EXPECT_NEAR(sum(b), 1.0f, 1e-4, "sum b");
    EXPECT_NEAR(sum(c), 1.0f, 1e-4, "sum c");
    return 0;
}
```

- [ ] **Step 2: Build to verify it fails**

Build. Expected: `settle_tail` undeclared.

- [ ] **Step 3: Write minimal implementation**

Declare in the class:
```cpp
  static std::vector<float> settle_tail(const std::vector<float>& tailDurs, float finalMin);
```

Define inline:
```cpp
inline std::vector<float> DefaultPhraseStrategy::settle_tail(
    const std::vector<float>& tailDurs, float finalMin) {
    int n = int(tailDurs.size());
    if (n <= 1) return tailDurs;
    float total = 0; for (float d : tailDurs) total += d;

    int A = n;
    for (; A > 1; --A) {
        float lead = 0; for (int i = 0; i < A - 1; ++i) lead += tailDurs[i];
        if (total - lead >= finalMin) break;   // final note long enough with A notes
    }
    std::vector<float> out;
    float lead = 0;
    for (int i = 0; i < A - 1; ++i) { out.push_back(tailDurs[i]); lead += tailDurs[i]; }
    out.push_back(total - lead);                // final absorbs the elided remainder
    return out;
}
```

- [ ] **Step 4: Build and run to verify it passes**

Build + run. Expected: `[TEST] test_settle_tail ... PASS`.

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add engine/include/mforce/music/default_strategies.h tools/test_figures/main.cpp
git -C /c/@dev/repos/mforce commit -m "feat(cadence): settle_tail elision-to-final-duration"
```

---

### Task 3: Rewrite `apply_cadence` to use the helpers

**Files:**
- Modify: `engine/include/mforce/music/default_strategies.h`

**Interfaces:**
- Consumes: `build_approach_steps`, `settle_tail` (Tasks 1-2), existing `degree_in_scale`.
- Reuses the existing cadence-figure location loop (finds `lastIdx`).

- [ ] **Step 1: Replace the single-note shift body**

In `apply_cadence`, the current body from `int startDeg = ...` through `targetFig.units.back().step += diff;` (default_strategies.h:307-331) is replaced. Keep the `lastIdx` location loop above it. New body after `if (lastIdx < 0) return;`:

```cpp
    int len = scale.length();
    int target = ((tmpl.cadenceTarget % len) + len) % len;

    // Degree at the END of the preserved head: start + all prior figures + this
    // figure's connector + this figure's head steps.
    int headEndDeg = degree_in_scale(phrase.startingPitch, scale);
    for (int f = 0; f < lastIdx; ++f) {
        if (f < int(phrase.connectors.size())) headEndDeg += phrase.connectors[f].leadStep;
        headEndDeg += phrase.figures[f]->net_step();
    }
    if (lastIdx < int(phrase.connectors.size())) headEndDeg += phrase.connectors[lastIdx].leadStep;

    auto& cf = *phrase.figures[lastIdx];
    int N = cf.note_count();
    if (N <= 0) return;
    int head = std::max(0, N - 3);
    for (int i = 0; i < head; ++i) headEndDeg += cf.units[i].step;

    // Original tail durations (preserve total figure duration).
    std::vector<float> tailDurs;
    for (int i = head; i < N; ++i) tailDurs.push_back(cf.units[i].duration);

    std::vector<float> newDurs = settle_tail(tailDurs, 1.0f);   // finalMin = 1.0 beat
    int A = int(newDurs.size());
    std::vector<int> appSteps = build_approach_steps(headEndDeg, target, A, len);

    cf.units.resize(head);                       // keep the head, drop the old tail
    for (int i = 0; i < A; ++i) {
        FigureUnit u;
        u.duration = newDurs[i];
        u.step = appSteps[i];
        cf.units.push_back(u);
    }
```

- [ ] **Step 2: Build `test_figures` and confirm the whole suite still passes**

Build `test_figures`, run the exe. Expected: all tests PASS (the existing integ tests still compose/render; the new unit tests pass). If an existing integ test asserted the *old* single-note behavior, note it — the cadence shape legitimately changed; update that test's expectation to the new reshape (do not silently weaken it).

- [ ] **Step 3: Build `mforce_cli`**

Run the cmake build with `--target mforce_cli`. Expected: exit 0.

- [ ] **Step 4: Integration-verify on the period sweep**

```bash
build/tools/mforce_cli/Debug/mforce_cli.exe --compose patches/Additive1.json renders/period_markov 20 --template patches/period_markov_test.json 2>&1 | grep -ci composed
python -c "
import json, statistics as st
NAMES=['C','C#','D','D#','E','F','F#','G','G#','A','A#','B']
ok=True
for i in range(1,21):
    ev=json.load(open(f'renders/period_markov_{i}.json'))['parts'][0]['events']
    notes=[int(e['data']['noteNumber']) for e in ev]
    durs=[e['data']['duration'] for e in ev]
    total=max(e['beat']+e['data']['duration'] for e in ev)
    if durs[-1] < st.median(durs): ok=False        # final note settled (>= median)
    if abs(total-16.0) > 1e-3: ok=False             # period still 16 beats
print('all finals settled (>= median) and 16 beats:', ok)
print('sample r1 last 4 (note,dur):', [(NAMES[notes[-k]%12], round(durs[-k],3)) for k in range(4,0,-1)])
"
```
Expected: `True`, and the last few notes of a render show a stepwise/leap approach into a longer final tonic (not a single yank). Listen to `renders/period_markov_1.wav … _20.wav`.

- [ ] **Step 5: Commit (checkpoint — ask Matt)**

```bash
git -C /c/@dev/repos/mforce add engine/include/mforce/music/default_strategies.h
git -C /c/@dev/repos/mforce commit -m "feat(cadence): head-preserve + distance-aware approach + settle in apply_cadence"
```

---

## Notes for the implementer

- **Rests in the tail:** v1 ignores the `rest` flag when reshaping — new tail units are pitched. Tail rests at a cadence are rare; flag if one shows up.
- **`net_step()` includes `units[0].step`** which is 0 by convention, so prior-figure sums are correct.
- **Don't weaken existing tests:** if an integ test encoded the old one-note yank, update it to assert the new reshape (head preserved, lands on target, final note longest), don't delete the assertion.
- **finalMin and the head rule are the tunables** — if Matt's audition wants a longer/shorter goal or more/less head, they're one-line changes (`1.0f`, `N-3`).
