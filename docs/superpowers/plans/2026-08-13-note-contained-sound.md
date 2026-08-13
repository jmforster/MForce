# Note-Contained Sound Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** All sound contained within note duration — release as the final envelope stage, damper as a ValueSource input, key-up gating in the UI, `noteOffFrame`/`instrument.release`/silence-reclaim deleted.

**Architecture:** The Envelope gains a gated mode (expand stage holds until `gate_release()`), an end-hold flag, a `damper` preset, and an engine-wide reflection allowance. KSPianoString's damper becomes a continuous input driven by a damper-preset Envelope. Voice lifetime becomes exactly `durSamples` everywhere. Spec: `docs/superpowers/specs/2026-08-13-note-contained-sound-design.md`.

**Tech Stack:** C++ (header-heavy engine, MSVC, CMake build in `build/`), nlohmann JSON loader, ImGui UI, Python verification harnesses driving `mforce_cli.exe`.

## Global Constraints

- Build from repo root; both targets (`mforce_cli`, `mforce_ui`) must build in every task that touches engine headers.
- `mforce_ui.exe` is RUNNING (Matt's session, pid may change). Use the run-18 rename-then-link approach: before building UI, rename the on-disk exe to `mforce_ui_locked_20260813.exe`; the link then succeeds; Matt's window is unaffected.
- No heap allocation in hot render loops. `next()` must not allocate.
- No patch back-compat shims — behavior changes just change the formula (Matt's standing policy).
- Two Claudes share this working copy: before every commit, verify branch is `main` live (`git -C C:/@dev/repos/mforce branch --show-current`) and stage explicit paths only (never `git add -A`).
- CLI binary: `build/tools/mforce_cli/Release/mforce_cli.exe`. Renders for verification go to the session scratchpad, NOT `renders/` roots.
- Audition WAVs land in `renders/dsp/pending/ks_piano_v6/` and also get copied per the "copy renders for Matt" convention (they're already in the main repo here, so no worktree copy needed).
- Containment floor ε = 1e-4 (−80 dBFS). Reflection allowance = 10 ms, introduced at 0.0 and flipped in Task 6 only.

---

### Task 1: Null-test manifest harness + pre-change baseline

The corpus null test needs a BEFORE snapshot rendered with the current HEAD
build, so this task must complete before any engine edit.

**Files:**
- Create: `tools/null_test_manifest.py`
- Output (scratchpad, not committed): `<scratchpad>/ncs_null/manifest_before.txt`

**Interfaces:**
- Produces: `manifest_before.txt` — lines of `sha256  relative-patch-path` (or `FAIL  path`), consumed by Task 5 and Task 6.

- [ ] **Step 1: Write the harness**

```python
#!/usr/bin/env python3
"""Render a patch set and emit a sha256 manifest — the before/after halves
of a corpus null test. Patches that fail to render record FAIL (a FAIL that
matches a FAIL is a pass for null purposes).

Usage: python tools/null_test_manifest.py <outfile> [dir ...]
Default dirs: patches/baselines patches/library
"""
import hashlib
import os
import subprocess
import sys
import glob
import tempfile

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release", "mforce_cli.exe")


def main():
    outfile = sys.argv[1]
    dirs = sys.argv[2:] or ["patches/baselines", "patches/library"]
    patches = []
    for d in dirs:
        patches += sorted(glob.glob(os.path.join(ROOT, d, "**", "*.json"),
                                    recursive=True))
    tmp = tempfile.mkdtemp(prefix="ncs_null_")
    lines = []
    for p in patches:
        rel = os.path.relpath(p, ROOT).replace("\\", "/")
        wav = os.path.join(tmp, "out.wav")
        if os.path.exists(wav):
            os.remove(wav)
        r = subprocess.run([CLI, p, wav], capture_output=True, timeout=600)
        if r.returncode != 0 or not os.path.exists(wav):
            lines.append("FAIL  %s" % rel)
            continue
        h = hashlib.sha256(open(wav, "rb").read()).hexdigest()
        lines.append("%s  %s" % (h, rel))
    with open(outfile, "w") as f:
        f.write("\n".join(lines) + "\n")
    n_fail = sum(1 for l in lines if l.startswith("FAIL"))
    print("%d patches, %d rendered, %d FAIL -> %s"
          % (len(lines), len(lines) - n_fail, n_fail, outfile))


if __name__ == "__main__":
    main()
```

- [ ] **Step 2: Run it against the CURRENT build (before any engine change)**

Run: `python tools/null_test_manifest.py "<scratchpad>/ncs_null/manifest_before.txt"`
(create the `<scratchpad>/ncs_null/` directory first)
Expected: several hundred patches, a small known FAIL count (pre-existing
non-renderable patches), manifest written. Record the counts.

- [ ] **Step 3: Verify branch, commit the harness only**

```bash
git -C C:/@dev/repos/mforce branch --show-current
git -C C:/@dev/repos/mforce add tools/null_test_manifest.py
git -C C:/@dev/repos/mforce commit -m "test(tools): corpus null-test manifest harness (note-contained sound prep)"
```

---

### Task 2: Envelope — gated mode, gate_release, end-hold, damper preset, allowance plumbing (at 0)

**Files:**
- Modify: `engine/include/mforce/core/envelope.h`
- Modify: `engine/src/patch_loader.cpp:334-374` (damper preset parse)

**Interfaces:**
- Produces: `Envelope::set_gated(bool)`, `int Envelope::gate_release()`
  (returns final-stage frame count; 0 if nothing to do),
  `Envelope::make_damper(int sampleRate, float releasePct, float releaseMin, float releaseMax)`,
  `Envelope::kReflectionAllowanceSec` (0.0f until Task 6),
  JSON `{"type":"Envelope","params":{"preset":"damper","release":0.5,"releaseMax":0.25}}`.
- Consumed by: Task 3 (v6m damper env), Task 7 (UI gating).

- [ ] **Step 1: Add members and allowance to `envelope.h`**

In the private section of `Envelope` (near `adsrLayout_`), add:

```cpp
  // Note-contained sound (2026-08-13 spec):
  // gated_: live-note mode — the expand stage holds until gate_release().
  // endHold_: past the last stage, hold its endVal instead of emitting 0
  //   (a damper stays down after landing; amplitude envelopes stay 0).
  // gateActive_/gateFrom_: final stage re-anchored to the value at
  //   gate_release() so a mid-attack key-up releases from where it was.
  bool  gated_{false};
  bool  endHold_{false};
  int   expandIdx_{-1};
  bool  gateActive_{false};
  float gateFrom_{0.0f};
```

In the public section add:

```cpp
  // Engine-wide reflection allowance: envelopes lay their stages out over
  // (frames - allowance) so bounded internal-reflection dispersal can
  // finish INSIDE the note. 0.0 until the corpus null test passes
  // (2026-08-13 plan Task 6 flips it to 0.010f).
  static constexpr float kReflectionAllowanceSec = 0.0f;

  void set_gated(bool g) { gated_ = g; }

  // Live note-off: jump to the final stage NOW, re-anchored to the current
  // output (click-free). Returns the final stage's frame count so the
  // caller can bound the voice's remaining lifetime. Safe to call from the
  // audio thread under the audio mutex — no allocation.
  int gate_release() {
    if (stages_.empty() || stageCounts_.empty()) return 0;
    int last = int(stages_.size()) - 1;
    if (currStage_ == last) return std::max(0, stageEnd_ - ptr_);
    gateFrom_ = cur_;
    gateActive_ = true;
    stageStart_ = ptr_ + 1;
    currStage_ = last;
    stageEnd_ = stageStart_ + stageCounts_[last];
    return stageCounts_[last];
  }
```

- [ ] **Step 2: Rework `prepare()` for allowance + expand tracking + gated minimum**

In `Envelope::prepare`, replace the opening two lines

```cpp
    totalFrames_ = frames;
    float duration = float(frames) / float(sampleRate_);
```

with

```cpp
    totalFrames_ = frames;
    const int allowFrames = int(kReflectionAllowanceSec * float(sampleRate_));
    const int layoutFrames = std::max(0, frames - allowFrames);
    float duration = float(layoutFrames) / float(sampleRate_);
```

then replace every later use of `frames` in the stage layout with
`layoutFrames` — three sites:

```cpp
        if (std::abs(totCount + stageCounts_[i] - layoutFrames) <= 1)
          stageCounts_[i] = layoutFrames - totCount;
```

```cpp
    if (expandIdx >= 0) {
      stageCounts_[expandIdx] = std::max(0, layoutFrames - totCount);
      // Gated notes need a live expand stage to hold in; a zero-count
      // expand (short nominal duration) would fall straight into release.
      if (gated_ && stageCounts_[expandIdx] == 0) stageCounts_[expandIdx] = 1;
    }
    expandIdx_ = expandIdx;
    gateActive_ = false;
```

(the `absolute_time` clamp `std::clamp(stgDur, 0.0f, duration)` already
uses `duration`, which is now layout-based — no edit needed there).

- [ ] **Step 3: Rework `next()` for hold, end-hold, and gate re-anchor**

Immediately after the `stageCounts_.empty()` guard, add the gated hold —
freeze the position instead of advancing off the expand stage:

```cpp
    if (gated_ && currStage_ == expandIdx_ && expandIdx_ >= 0 &&
        ptr_ + 1 >= stageEnd_) {
      // Held note: sit on the expand stage until gate_release().
      cur_ = stages_[currStage_].ramp.value(1.0f);
      if (ramp_accuracy < 1.0f)
        cur_ *= 1.0f + (1.0f - ramp_accuracy) * lfo_next_();
      return cur_;
    }
```

Replace the past-last-stage block

```cpp
    if (ptr_ >= stageEnd_ && currStage_ == int(stages_.size()) - 1) {
      cur_ = 0.0f;
      return cur_;
    }
```

with

```cpp
    if (ptr_ >= stageEnd_ && currStage_ == int(stages_.size()) - 1) {
      cur_ = endHold_ ? stages_.back().ramp.endVal : 0.0f;
      return cur_;
    }
```

And after `cur_ = stages_[currStage_].ramp.value(pos);` add the re-anchor:

```cpp
    if (gateActive_ && currStage_ == int(stages_.size()) - 1) {
      const Ramp& r = stages_[currStage_].ramp;
      float denom = r.endVal - r.startVal;
      float shape = (std::fabs(denom) > 1e-9f) ? (cur_ - r.startVal) / denom
                                               : pos;
      cur_ = gateFrom_ + (r.endVal - gateFrom_) * shape;
    }
```

- [ ] **Step 4: Add `make_damper` factory**

After `make_adsr_abs` in `envelope.h`:

```cpp
  // Damper control envelope (note-contained sound, 2026-08-13): holds 0
  // (open) through the note; the FINAL stage ramps 0 -> 1 — the damper
  // falling IS the release phase, expressed as the last Stage of an
  // Envelope like every amplitude ADSR. Standard Stage triple: a generous
  // pct + maxSec cap = constant release on normal notes, proportional
  // compression on short ones (Matt's formulation). endHold: a damper
  // stays down after landing.
  static Envelope make_damper(int sampleRate, float releasePct,
                              float releaseMin, float releaseMax) {
    Envelope env(sampleRate);
    env.add_stage({{0.0f, 0.0f, RampType::Linear, 0.0f}, 0.0f, 0.0f, 0.0f}); // open (expand)
    env.add_stage({{0.0f, 1.0f, RampType::Linear, 0.0f}, releasePct, releaseMin, releaseMax});
    env.endHold_ = true;
    return env;
  }
```

- [ ] **Step 5: Loader — parse the damper preset**

In `engine/src/patch_loader.cpp`, in the preset chain (after the `adsr`
branch closing at line ~372, before the `else throw`):

```cpp
                } else if (preset == "damper") {
                    env = std::make_shared<Envelope>(Envelope::make_damper(sampleRate,
                        p.value("release", 0.5f),
                        p.value("releaseMin", 0.0f), p.value("releaseMax", 0.0f)));
                } else {
```

- [ ] **Step 6: Build both targets**

Rename the locked UI exe first (PowerShell):
`Rename-Item "C:/@dev/repos/mforce/build/tools/mforce_ui/Release/mforce_ui.exe" "mforce_ui_locked_20260813.exe"`
(adjust path to the actual UI output dir — locate with `Glob build/**/mforce_ui.exe` first; skip if already renamed).
Then build from repo root exactly as the repo's established build command does (cmake --build build --config Release).
Expected: both targets link clean.

- [ ] **Step 7: Spot null check (fast, before the full Task 5 sweep)**

Render 3 diverse baseline patches (e.g. `patches/baselines/add_string_test.json`,
`patches/baselines/bend_test_pluck.json`, `patches/baselines/fadd_vowel_morph_test.json`)
to scratchpad and sha256-compare against the same three hashes in
`manifest_before.txt`. Expected: identical (allowance is 0.0, gated_ defaults
false, endHold_ false — pure no-op for existing patches).

- [ ] **Step 8: Commit**

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/core/envelope.h engine/src/patch_loader.cpp
git -C C:/@dev/repos/mforce commit -m "feat(engine): Envelope gated mode + gate_release + damper preset + allowance plumbing (at 0)"
```

---

### Task 3: KSPianoString — damper as ValueSource input, noteOffFrame logic removed from the node

**Files:**
- Modify: `engine/include/mforce/source/ks_piano_string.h`

**Interfaces:**
- Produces: input `damper` on KSPianoString (0 = open → 1 = damped), wired
  in JSON as `"damper": {"ref": "env_damper"}`. `releaseFb`/`damperNoise`
  configs unchanged in meaning.
- Consumes: nothing new (works with any ValueSource; Task 2's damper preset
  is the intended driver).

- [ ] **Step 1: Add the input**

In `input_descriptors()`:

```cpp
    static constexpr InputDescriptor descs[] = {
      {"source"},
      {"damper"},
    };
```

In `set_param` / `get_param`:

```cpp
    if (name == "damper")    { damper_    = std::move(src); return; }
```
```cpp
    if (name == "damper")    return damper_;
```

Member (near `source_`): `std::shared_ptr<ValueSource> damper_;`

- [ ] **Step 2: Replace note-off state with damper state**

Delete members `int noteOffFrame_{-1};` and `int frame_{0};`
Add member `float lastD_{0.0f};`

In `prepare()`: delete `noteOffFrame_ = ctx.noteOffFrame;` and `frame_ = 0;`
add `lastD_ = 0.0f;` and `if (damper_) damper_->prepare(ctx, frames);`
(alongside the other child prepares).

- [ ] **Step 3: Rework `next()` — continuous damp + crossing-triggered thud**

Replace the block from `float damp = 1.0f;` through `++frame_;` with:

```cpp
    // In-loop damper, now a continuous input (0 = open, 1 = fully damped).
    // d scales the loop gain toward releaseFb — at 1 identical to the old
    // post-note-off choke, in between it's a partially lifted damper
    // (half-pedaling). Driven by a damper-preset Envelope whose final
    // stage IS the note's release phase (note-contained sound, 2026-08-13).
    float d = 0.0f;
    if (damper_) { damper_->next(); d = std::clamp(damper_->current(), 0.0f, 1.0f); }
    float damp = 1.0f - d * (1.0f - releaseFb_);
    // Damper-contact noise: each time the felt LANDS (rising threshold
    // crossing), capture the ring level and ring a short burst through the
    // now-damped string. Re-arms when the damper lifts — a re-dropped
    // damper on a still-ringing string thuds again, scaled by what's left.
    float dnoise = 0.0f;
    if (damperNoise_ > 0.0001f) {
      if (d >= 0.05f && lastD_ < 0.05f) dnAmp_ = damperNoise_ * envFollow_;
      if (dnAmp_ > 1e-6f) {
        dnoise = dnRng_.valuePN() * dnAmp_;
        dnAmp_ *= 0.9995f;   // ~-60 dB over ~28 ms at 48 kHz
      }
    }
    lastD_ = d;
```

Everything downstream (`damp` applied at `inharmFb_ * damp * hp` and
`c.gain * damp * c.lp`) is unchanged.

- [ ] **Step 4: Build both targets, render one KS patch**

Build as in Task 2 Step 6. Render `patches/pending/ks_piano_v6/v6l_cmaes3.json`
to scratchpad. Expected: renders clean; the release tail now RINGS
undamped through the (still-present) 2 s release window, because nothing
wires `damper` yet — this is the expected intermediate state, do not
"fix" it. The v6 family is outside the null set.

- [ ] **Step 5: Commit**

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/source/ks_piano_string.h
git -C C:/@dev/repos/mforce commit -m "feat(engine): KSPianoString damper is a ValueSource input; noteOffFrame logic removed from the node"
```

---

### Task 4: Voice lifetime = durSamples — remove releaseSeconds, noteOffFrame, UI release plumbing; containment warn

**Files:**
- Modify: `engine/include/mforce/core/render_context.h`
- Modify: `engine/include/mforce/render/instrument.h`
- Modify: `engine/src/patch_loader.cpp:866,925,1059`
- Modify: `tools/mforce_ui/main.cpp` (compile fixes only — the reclaim
  removal and gating feature are Task 7)

**Interfaces:**
- Produces: `StreamingVoice{source, durSamples, gain}` (releaseSamples
  gone); `prepare_voice(note, vel, duration)` prepares exactly durSamples;
  `play_note` renders exactly durSamples and emits the containment warn.
- Consumed by: Task 7 (UI schedules voices with the new StreamingVoice).

- [ ] **Step 1: RenderContext — delete noteOffFrame**

`engine/include/mforce/core/render_context.h` becomes:

```cpp
#pragma once
namespace mforce {

// Ambient render state propagated top-down through ValueSource::prepare.
// Intentionally minimal; add fields only when a consumer needs one.
// noteOffFrame was removed 2026-08-13 (note-contained sound): release is
// the final stage of an Envelope inside the note's duration, so no node
// needs to know where the gate ends.
struct RenderContext {
    int sampleRate;
};

} // namespace mforce
```

(match the file's actual namespace/includes when editing — keep whatever
surrounds the struct today, only the field and its comment change).

- [ ] **Step 2: instrument.h — prepare_voice**

Delete member `float releaseSeconds{0.0f};` and its comment block
(lines ~148-151). In `StreamingVoice` delete the `releaseSamples` field
and its comment. In `prepare_voice` replace

```cpp
    int relSamples = int(releaseSeconds * float(sampleRate));
    RenderContext ctx{ sampleRate, durSamples };
    vg.source->prepare(ctx, durSamples + relSamples);

    return { vg.source, durSamples, gain, relSamples };
```

with

```cpp
    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, durSamples);

    return { vg.source, durSamples, gain };
```

- [ ] **Step 3: instrument.h — play_note + containment warn**

Add `#include <cstdio>` to the header's includes. In `play_note` replace
from `int relSamples = ...` through the release-fade loop with:

```cpp
    RenderContext ctx{ sampleRate };
    vg.source->prepare(ctx, durSamples);

    std::vector<float> buf(durSamples);
    for (int i = 0; i < durSamples; ++i)
      buf[i] = vg.source->next() * gain;

    // Note-contained-sound check (2026-08-13 spec): output must be at the
    // audibility floor by duration end. WARN, never fail — a miss is a
    // patch-design finding, and optimizer runs must keep scoring.
    int checkStart = std::max(0, durSamples - sampleRate / 1000);
    float tailPeak = 0.0f;
    for (int i = checkStart; i < durSamples; ++i)
      tailPeak = std::max(tailPeak, std::fabs(buf[i]));
    if (tailPeak > 1e-4f)
      std::fprintf(stderr,
          "[containment] note %.1f (%.1f Hz) at t=%.2fs: %.1f dBFS in final 1 ms\n",
          noteNumber, freq, startTime, 20.0f * std::log10(tailPeak));

    add_rendered(startTime, buf.data(), durSamples);
```

- [ ] **Step 4: Loader — retire instrument.release, fix prepared length**

At `patch_loader.cpp:866` and `:1059`, replace
`inst->releaseSeconds = instJson.value("release", 0.0f);` with:

```cpp
    if (instJson.value("release", 0.0f) != 0.0f)
        std::fprintf(stderr, "[loader] instrument.release retired "
                     "(note-contained sound 2026-08-13); ignored\n");
```

At `:925`, read the surrounding lines first; change the prepared-length
formula `(maxEnd + inst->releaseSeconds + 0.5) * sampleRate` to
`maxEnd * sampleRate` and update its comment to say the release+tail
window is gone. Ensure `<cstdio>` is included.

- [ ] **Step 5: UI compile fixes (minimal)**

In `tools/mforce_ui/main.cpp` `play_note` (~2843-2853), replace the
schedule block with:

```cpp
        auto sv = pitched->prepare_voice(noteNum, velocity, durationSeconds);
        voice_schedule(ip, sv.source, sv.durSamples, sv.gain, int(noteNum),
                       0, 1.0f);
```

(the fade args become inert here; Task 7 removes them from Voice
entirely — this step is only about compiling against the new
StreamingVoice).

- [ ] **Step 6: Build both targets (rename-then-link), run the containment smoke**

Build. Then render `patches/baselines/bend_test_pluck.json` to scratchpad.
Expected: builds clean; render succeeds; note whether `[containment]`
warns appear (an envelope patch whose release stage ends at duration
should be silent at the floor — a warn here is information, record it,
don't chase it in this task).

- [ ] **Step 7: Commit**

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/core/render_context.h engine/include/mforce/render/instrument.h engine/src/patch_loader.cpp tools/mforce_ui/main.cpp
git -C C:/@dev/repos/mforce commit -m "feat(engine+ui): voice lifetime = durSamples exactly — releaseSeconds/noteOffFrame retired, containment warn"
```

---

### Task 5: Corpus null test at allowance 0

**Files:**
- Output (scratchpad): `<scratchpad>/ncs_null/manifest_after0.txt`

**Interfaces:**
- Consumes: `manifest_before.txt` (Task 1), the Task 2-4 build.

- [ ] **Step 1: Render the after-manifest**

Run: `python tools/null_test_manifest.py "<scratchpad>/ncs_null/manifest_after0.txt"`

- [ ] **Step 2: Diff**

Run: `diff <scratchpad>/ncs_null/manifest_before.txt <scratchpad>/ncs_null/manifest_after0.txt`
Expected: **empty diff** — every baseline/library patch byte-identical
(none carries nonzero `instrument.release`, measured 2026-08-13; the
engine changes are inert at allowance 0 for unwired patches).
If ANY hash moved: STOP, bisect that patch (its graph reaches new code —
find how) before proceeding. Do not rationalize a diff away.

- [ ] **Step 3: Record the result**

Append the counts and "identical: yes/no" to the task notes for the final
report. No commit (nothing changed).

---

### Task 6: Flip the reflection allowance to 10 ms

**Files:**
- Modify: `engine/include/mforce/core/envelope.h` (one constant)

- [ ] **Step 1: Flip**

```cpp
  static constexpr float kReflectionAllowanceSec = 0.010f;
```

Update the constant's comment to past tense (flipped after the null test
passed).

- [ ] **Step 2: Rebuild both targets, re-render manifest**

Run: `python tools/null_test_manifest.py "<scratchpad>/ncs_null/manifest_after10.txt"`
Expected: hashes CHANGE broadly (every enveloped patch's layout shifts
10 ms) — this is the intentional corpus-wide change, per spec and the
no-backcompat policy. FAIL set unchanged.

- [ ] **Step 3: Spot-measure the shift is exactly the allowance**

Render `patches/baselines/add_string_test.json` to scratchpad; in Python,
find the last sample with |x| > 1e-4 relative to the score's note ends.
Expected: the envelope reaches the floor ~10 ms (480 samples at 48 k)
before each note's duration end, ±2 samples. Show the measured number.

- [ ] **Step 4: Commit**

```bash
git -C C:/@dev/repos/mforce add engine/include/mforce/core/envelope.h
git -C C:/@dev/repos/mforce commit -m "feat(engine): reflection allowance 0 -> 10 ms — corpus-wide layout shift, null test run before/after"
```

---

### Task 7: UI — key-up gating, reclaim removal, timeMode exposure

**Files:**
- Modify: `tools/mforce_ui/main.cpp`

**Interfaces:**
- Consumes: `Envelope::set_gated/gate_release` (Task 2),
  `StreamingVoice{source,durSamples,gain}` (Task 4).

- [ ] **Step 1: Strip the reclaim + fade plumbing from Voice**

In `struct Voice` (~2183): delete `fadeBelowRemaining`, `fadeDecay`,
`silentRun` and their comments. Add:

```cpp
    // Live-gated note (key held): sustains until key-up fires
    // gate_release() on its envelopes. envs are non-owning — the ip
    // shared_ptr held by this Voice keeps the graph alive.
    bool held = false;
    std::vector<mforce::Envelope*> envs;
```

`voice_schedule` (~2197): drop the `fadeBelowRemaining`/`fadeDecay`
parameters; add `bool held` and `std::vector<mforce::Envelope*> envs`
parameters, defaulting `{false, {}}`; set the fields; keep everything
else. Fix its call sites.

In `audio_callback` (~2257): the voice loop becomes

```cpp
        for (int v = 0; v < MAX_VOICES; ++v) {
            auto& voice = g_voices[v];
            if (!voice.active) continue;
            voiceSum += voice.source->next() * voice.gain;
            voice.samplesRemaining--;
            if (voice.samplesRemaining <= 0) voice.active = false;
        }
```

(no silence sniffer, no fade — a held voice carries a huge
samplesRemaining until key-up rewrites it).

- [ ] **Step 2: Envelope collection helper**

Above `play_note` add:

```cpp
// Walk a loaded voice graph collecting Envelope nodes (for live gating).
// Returns false if the graph contains a MultiplexSource — its internal
// clones are not reachable by this walk, so gating the template would
// silently do nothing; callers fall back to scheduled notes there.
static bool collect_envelopes(mforce::ValueSource* vs,
                              std::vector<mforce::Envelope*>& out,
                              std::vector<mforce::ValueSource*>& seen) {
    if (!vs) return true;
    for (auto* s : seen) if (s == vs) return true;
    seen.push_back(vs);
    if (std::string_view(vs->type_name()).find("Multiplex") != std::string_view::npos)
        return false;
    if (auto* env = dynamic_cast<mforce::Envelope*>(vs)) out.push_back(env);
    bool ok = true;
    for (const auto& d : vs->input_descriptors())
        ok = collect_envelopes(vs->get_param(d.name).get(), out, seen) && ok;
    for (const auto& d : vs->param_descriptors())
        ok = collect_envelopes(vs->get_param(d.name).get(), out, seen) && ok;
    return ok;
}
```

(verify the descriptor field is `.name` in `dsp_value_source.h` while
implementing; adjust if it differs).

- [ ] **Step 3: Held-note play + release**

After `play_note` add:

```cpp
// Key-down entry: hold the note until the matching key-up. Envelopes are
// flipped to gated mode BEFORE prepare so the expand stage holds; pct
// stages resolve against the transport duration as nominal. Falls back to
// a scheduled note when the graph's envelopes aren't reachable
// (MultiplexSource) or absent.
static void play_note_held(float noteNum, float velocity, float nominalSeconds) {
    if (s_graphMode != GraphMode::PatchGraph) { play_note(noteNum, velocity, nominalSeconds); return; }
    note_played(noteNum);
    std::string path = get_playback_patch_path();
    if (path.empty()) return;
    try {
        auto ip = std::make_shared<InstrumentPatch>(load_instrument_patch(path));
        auto* pitched = ip->instrument.get();
        if (!pitched) return;
        std::vector<mforce::Envelope*> envs;
        std::vector<mforce::ValueSource*> seen;
        bool gateable = true;
        for (auto& vg : pitched->voicePool)
            gateable = collect_envelopes(vg.source.get(), envs, seen) && gateable;
        if (!gateable || envs.empty()) {
            play_note(noteNum, velocity, nominalSeconds);   // scheduled fallback
            return;
        }
        for (auto* e : envs) e->set_gated(true);
        auto sv = pitched->prepare_voice(noteNum, velocity, nominalSeconds);
        voice_schedule(ip, sv.source, INT_MAX / 2, sv.gain, int(noteNum),
                       true, std::move(envs));
    } catch (const std::exception& e) {
        char buf[256];
        std::snprintf(buf, sizeof(buf), "play_note_held failed: %s", e.what());
        transport_set_status(buf, true);
    }
}

// Key-up: gate the matching held voice's envelopes and bound its life to
// the longest release + the reflection allowance.
static void release_note_held(int midiNote) {
    std::lock_guard<std::mutex> lock(g_audioMutex);
    for (int v = 0; v < MAX_VOICES; ++v) {
        auto& voice = g_voices[v];
        if (!voice.active || !voice.held || voice.midiNote != midiNote) continue;
        int maxRel = 0;
        for (auto* e : voice.envs) maxRel = std::max(maxRel, e->gate_release());
        int allow = int(mforce::Envelope::kReflectionAllowanceSec * AUDIO_SAMPLE_RATE);
        voice.samplesRemaining = maxRel + allow + 1;
        voice.held = false;
        return;
    }
}
```

(`play_note_held` mirrors `play_note`'s existing load pattern — check the
actual member names `ip->instrument`, `pitched->voicePool` against the
2830-2853 region while implementing and match them exactly. `INT_MAX`
needs `<climits>`.)

- [ ] **Step 4: QWERTY key-up wiring**

Add file-scope `static int s_qwertyHeldNote[QWERTY_MAP_COUNT];` initialized
to -1 (static init). Replace the QWERTY loop (~4069-4074) with:

```cpp
        for (int i = 0; i < QWERTY_MAP_COUNT; ++i) {
            if (ImGui::IsKeyPressed(s_qwertyMap[i].key, false)) {
                int absNote = g_keyboard.octave * 12 + s_qwertyMap[i].offset;
                s_qwertyHeldNote[i] = absNote;
                play_note_held(float(absNote), g_transport.velocity, g_keyboard.duration);
            }
            if (ImGui::IsKeyReleased(s_qwertyMap[i].key) && s_qwertyHeldNote[i] >= 0) {
                release_note_held(s_qwertyHeldNote[i]);
                s_qwertyHeldNote[i] = -1;
            }
        }
```

(held-note bookkeeping is per-key so an octave change mid-hold still
releases the right note; mouse-clicked piano keys at ~4142/4172 stay on
scheduled `play_note` — unchanged).

- [ ] **Step 5: timeMode checkbox on the Envelope node panel**

In the Envelope property panel (~5325, above the "Stages" table header):

```cpp
            bool absTime = env->absolute_time;
            if (ImGui::Checkbox("seconds (timeMode)", &absTime)) {
                env->absolute_time = absTime;
                changed = true;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("off: stage 'percent' is a fraction of note duration\n"
                                  "on:  stage 'percent' is literal seconds (run-22 semantics)");
```

(match the surrounding panel's `changed` bookkeeping — read the block
first; the save path already serializes `timeMode`).

- [ ] **Step 6: Build (rename-then-link), manual smoke, stamp**

Build both targets. Run `mforce_ui --stamp` → expect exit 0.
Manual checks (needs the NEW exe — either ask Matt to restart or verify
with the new binary in a second instance if the audio device allows):
listed for Matt's next session if not verifiable now:
1. QWERTY hold: note sustains past transport duration, releases on key-up.
2. Rolled chord released together: one collective damper event (v6m patch,
   after Task 8).
3. Envelope node shows the timeMode checkbox; toggling + save round-trips.
Record which checks ran vs. deferred.

- [ ] **Step 7: Commit**

```bash
git -C C:/@dev/repos/mforce add tools/mforce_ui/main.cpp
git -C C:/@dev/repos/mforce commit -m "feat(ui): key-up gated notes via Envelope::gate_release; silence reclaim removed; timeMode exposed"
```

---

### Task 8: v6m reconstruction + mechanical A/B verification + audition renders

**Files:**
- Create: `tools/gen_v6m.py`
- Create: `patches/pending/ks_piano_v6/v6m.json` (generated)
- Output: `renders/dsp/pending/ks_piano_v6/v6m.wav` (calibrated),
  scratchpad A/B artifacts

**Interfaces:**
- Consumes: damper preset JSON (Task 2), `string.damper` input (Task 3).

- [ ] **Step 1: Write the generator**

```python
#!/usr/bin/env python3
"""v6m = v6l_cmaes3 restated under note-contained sound (2026-08-13 spec):
verb node removed (string -> output), env_damper (preset damper, pct 0.5 /
max 0.25 s) wired to string.damper, instrument.release removed. Everything
else byte-identical to v6l. Volume left unfolded — render_ks_piano_v6.py
recalibrates to 0.85 peak anyway.
"""
import json
import os

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(ROOT, "patches", "pending", "ks_piano_v6", "v6l_cmaes3.json")
DST = os.path.join(ROOT, "patches", "pending", "ks_piano_v6", "v6m.json")


def main():
    p = json.load(open(SRC))
    nodes = p["graph"]["nodes"]
    nodes = [n for n in nodes if n["id"] != "verb"]
    nodes.append({
        "id": "env_damper",
        "type": "Envelope",
        "params": {"preset": "damper", "release": 0.5, "releaseMax": 0.25},
    })
    for n in nodes:
        if n["id"] == "string":
            n["params"]["damper"] = {"ref": "env_damper"}
    p["graph"]["nodes"] = nodes
    p["graph"]["output"] = "string"
    p["instrument"].pop("release", None)
    json.dump(p, open(DST, "w"), indent=2)
    print("wrote", DST)


if __name__ == "__main__":
    main()
```

Run it. Then render v6m once to scratchpad to prove it loads and the
damper wires (no loader error, no `[loader] instrument.release` warn).

- [ ] **Step 2: Mechanical A/B — v6m vs v6l_null**

Write and run a scratchpad script (not committed) that:
1. Builds `v6l_null.json` in scratchpad: v6l_cmaes3 with the verb's
   `wet: 0.0, dry: 1.0`, `instrument` `release` key REMOVED (so both render
   exactly durSamples), same volume as v6m.json.
2. Renders both with the same score (the embedded C246 score).
3. For each note (starts 0.0/8.0/16.0 s, duration 3.5 s): compares int16
   samples over `[start, start + 3.5 − 0.25 − 0.010)` seconds.
Expected: **zero differing samples** in every compared region (identical
graphs and seeds up to the point the damper engages). Differences allowed
only in each note's final 0.26 s. If the early region differs: STOP —
the reconstruction is wrong (wrong wiring, wrong volume, or the damper
engaging early); diagnose before continuing.

- [ ] **Step 3: Containment + level sweep on v6m**

Run: `python tools/measure_ks_v6_levels.py patches/pending/ks_piano_v6/v6m.json "<scratchpad>/ks_v6m_sweep"`
Expected: sweep completes; stderr shows **zero `[containment]` warns**
(t60 at C2 is 15 s but the damper chokes every note by duration end);
attack-peak column ≈ the 2026-08-13 v6l sweep (verb wet was 0.18 — small
level shift OK, note it). The octave-7/8 level blowup and tuning errors
WILL still be present — they are the level-curve/comb-floor findings, out
of scope here; do not chase.

- [ ] **Step 4: Calibrated audition render + README**

Run `python tools/render_ks_piano_v6.py` (it sweeps the whole pending dir;
confirm it re-renders v6m.wav calibrated to 0.85 — if rendering ALL
patches is undesirable, temporarily point it at v6m only by moving other
JSONs aside is NOT allowed; instead add a quick argv filter if needed).
Append to `renders/dsp/pending/ks_piano_v6/README.md`:

```markdown
## v6m: note-contained sound reconstruction (2026-08-13)

v6l_cmaes3 restated under the 2026-08-13 spec: verb node REMOVED (room
reverb belongs downstream, not in the patch), damper is now an Envelope
(preset damper, pct 0.5 / max 0.25 s) wired to string.damper — the
damper falling IS the release phase, inside the note. instrument.release
gone; all sound ends by duration end (containment-checked across the
87-note sweep, zero warns).

A/B: v6m vs v6l_cmaes3. Two audible changes by construction: no reverb
(drier), and the damper lands 0.25 s BEFORE each note's end rather than
after it. Mechanically verified: identical to v6l (verb bypassed) to the
sample everywhere before the damper engages.

NOTE: CMA-ES scores are re-baselined by the verb removal — run3's 0.5111
is not comparable to future runs on v6m descendants.
```

- [ ] **Step 5: Commit**

```bash
git -C C:/@dev/repos/mforce add tools/gen_v6m.py patches/pending/ks_piano_v6/v6m.json renders/dsp/pending/ks_piano_v6/README.md
git -C C:/@dev/repos/mforce commit -m "feat(patches): v6m — v6l under note-contained sound (damper envelope, verb removed); mechanical A/B verified"
```

(README lives under renders/ which is gitignored — if `git add` refuses
it, keep the README edit uncommitted; that is the existing convention.)

---

### Task 9: Patch linter — instrument.release is a hard finding

**Files:**
- Modify: `tools/lint_patches.py`

- [ ] **Step 1: Add the check**

Read the linter's finding-emission pattern first, then add, in its
per-patch loop (matching its existing style):

```python
    inst = data.get("instrument")
    if isinstance(inst, dict) and "release" in inst:
        findings.append((relpath,
            "instrument.release retired 2026-08-13 (note-contained sound): "
            "remove the key; release is the final stage of the patch's envelope"))
```

- [ ] **Step 2: Run the linter**

Run: `python tools/lint_patches.py`
Expected: the 54 ks_piano pending/sweep patches (and only those) gain the
new finding; the count of OTHER findings is unchanged from before this
task (compare against a pre-change run captured in Step 1 if the tool is
noisy). v6m.json itself must NOT be flagged.

- [ ] **Step 3: Commit**

```bash
git -C C:/@dev/repos/mforce add tools/lint_patches.py
git -C C:/@dev/repos/mforce commit -m "chore(lint): flag retired instrument.release"
```

---

### Task 10: Docs — REVIEW queue entry, verdict trail

**Files:**
- Modify: `docs/autonomy/dsp/REVIEW.md`

- [ ] **Step 1: Append to item 27 (or a new item 28)**

Add a dated block: note-contained sound LANDED (spec + plan paths, commit
shas); v6m queued for ears as `renders/dsp/pending/ks_piano_v6/v6m.wav`
A/B vs v6l_cmaes3 (two by-construction differences: no verb, damper
inside the note); corpus null test result (identical at allowance 0,
corpus-wide 10 ms shift flipped after); CMA-ES re-baseline caveat; UI
gating verification checklist status from Task 7 Step 6 (which manual
checks ran vs. await Matt's restarted UI).

- [ ] **Step 2: Commit**

```bash
git -C C:/@dev/repos/mforce add docs/autonomy/dsp/REVIEW.md
git -C C:/@dev/repos/mforce commit -m "docs(dsp): note-contained sound landed — v6m queued, null-test record, re-baseline caveat"
```

---

## Self-Review

- Spec coverage: §1 damper input → Task 3; §2 damper preset + gate_release
  + timeMode UI → Tasks 2, 7; §3 lifetime/allowance/invariant/linter →
  Tasks 4, 5, 6, 9; §4 key-up gating → Task 7; §5 v6m + verb removal +
  README → Task 8; verification ladder steps 1-8 of the spec map to Tasks
  2-8 (spec step 8, the 10-voice perf check, folds into Task 7 Step 6's
  manual list). Library keepers with Reverb: untouched (spec: Matt's call,
  separately). Master-bus reverb, FIR node, articulation: non-goals, no
  tasks — correct.
- Placeholders: none — every code step carries the code; the two
  "read the surrounding lines first" notes (loader :925, linter style)
  are deliberate adapt-to-context instructions with the change specified.
- Type consistency: `set_gated(bool)`, `int gate_release()`,
  `make_damper(int, float, float, float)`, `StreamingVoice{source,
  durSamples, gain}`, `voice_schedule(..., bool held, vector<Envelope*>)`
  are used consistently across Tasks 2/4/7/8.
