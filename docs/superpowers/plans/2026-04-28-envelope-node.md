# Multi-stage Envelope Node Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the bare `Envelope` UI node with a real multi-stage editor: an inspector table for per-stage values plus add/remove. Preset envelopes (AREnvelope, ADSREnvelope, etc.) untouched.

**Architecture:** Engine adds typed accessors (`stage_count`, `stage(i)`, `add_stage_default`, `remove_stage`) on the existing `Envelope` struct. Patch loader gains a `stages` JSON branch (legacy `preset` branch preserved). UI replaces the `NT_ENVELOPE` ADSR-hardcode with a bare `Envelope` and renders an ImGui stage table that reads/writes the live DSP object directly.

**Tech Stack:** C++ 20, ImGui, nlohmann::json, CMake (MSVC Release).

**Spec:** `docs/superpowers/specs/2026-04-28-envelope-node-design.md`

---

### Task 1: Engine — add stage accessors and default-add

**Files:**
- Modify: `engine/include/mforce/core/envelope.h`

- [ ] **Step 1: Add accessors and `add_stage_default` / `remove_stage`**

In `engine/include/mforce/core/envelope.h`, locate the existing `add_stage(Stage s)` method (around line 80) and add immediately after it:

```cpp
  int stage_count() const { return int(stages_.size()); }
  Stage&       stage(int i)       { return stages_[i]; }
  const Stage& stage(int i) const { return stages_[i]; }

  // Append a stage with sensible defaults. startVal continues the prior
  // stage's endVal (or 0 if empty).
  void add_stage_default() {
    Stage s;
    s.ramp.type     = RampType::Linear;
    s.ramp.power    = 0.0f;
    s.ramp.holdPct  = 0.0f;
    s.ramp.startVal = stages_.empty() ? 0.0f : stages_.back().ramp.endVal;
    s.ramp.endVal   = 0.0f;
    s.percent = 0.2f;
    s.minSec  = 0.0f;
    s.maxSec  = 99.0f;
    stages_.push_back(s);
  }

  void remove_stage(int i) {
    if (i >= 0 && i < int(stages_.size())) stages_.erase(stages_.begin() + i);
  }
```

- [ ] **Step 2: Build engine to confirm header compiles**

Run:
```
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_cli
```
Expected: success.

- [ ] **Step 3: Commit**

```
git add engine/include/mforce/core/envelope.h
git commit -m "feat(envelope): add stage accessors + add_stage_default/remove_stage"
```

---

### Task 2: Patch loader — `stages` JSON branch

**Files:**
- Modify: `engine/src/patch_loader.cpp:258-274`

- [ ] **Step 1: Replace the `Envelope` branch with the dual-path version**

Replace the existing block:

```cpp
        else if (type == "Envelope") {
            if (!pp) throw std::runtime_error("Envelope requires params");
            const auto& p = *pp;
            std::string preset = p.value("preset", std::string("ar"));
            std::shared_ptr<Envelope> env;
            if (preset == "ar") {
                env = std::make_shared<Envelope>(Envelope::make_ar(sampleRate,
                    p.value("attack", 0.2f), p.value("attackMin", 0.0f), p.value("attackMax", 1.0f)));
            } else if (preset == "adsr") {
                env = std::make_shared<Envelope>(Envelope::make_adsr(sampleRate,
                    p.value("attack", 0.2f), p.value("decay", 0.1f),
                    p.value("sustainLevel", 0.7f), p.value("release", 0.0f)));
            } else {
                throw std::runtime_error("Unknown envelope preset: " + preset);
            }
            valueNodes[id] = env;
        }
```

with:

```cpp
        else if (type == "Envelope") {
            if (!pp) throw std::runtime_error("Envelope requires params");
            const auto& p = *pp;

            if (p.contains("stages")) {
                auto env = std::make_shared<Envelope>(sampleRate);
                for (const auto& sj : p["stages"]) {
                    Envelope::Stage s;
                    s.ramp.startVal = sj.value("startVal", 0.0f);
                    s.ramp.endVal   = sj.value("endVal",   0.0f);
                    s.ramp.power    = sj.value("power",    0.0f);
                    s.ramp.holdPct  = sj.value("holdPct",  0.0f);
                    std::string t   = sj.value("type", std::string("Linear"));
                    s.ramp.type = (t == "Expo")        ? RampType::Expo
                                : (t == "InverseExpo") ? RampType::InverseExpo
                                : (t == "Sine")        ? RampType::Sine
                                                       : RampType::Linear;
                    s.percent = sj.value("percent", 0.0f);
                    s.minSec  = sj.value("minSec",  0.0f);
                    s.maxSec  = sj.value("maxSec",  0.0f);
                    env->add_stage(s);
                }
                valueNodes[id] = env;
            } else {
                std::string preset = p.value("preset", std::string("ar"));
                std::shared_ptr<Envelope> env;
                if (preset == "ar") {
                    env = std::make_shared<Envelope>(Envelope::make_ar(sampleRate,
                        p.value("attack", 0.2f), p.value("attackMin", 0.0f), p.value("attackMax", 1.0f)));
                } else if (preset == "adsr") {
                    env = std::make_shared<Envelope>(Envelope::make_adsr(sampleRate,
                        p.value("attack", 0.2f), p.value("decay", 0.1f),
                        p.value("sustainLevel", 0.7f), p.value("release", 0.0f)));
                } else {
                    throw std::runtime_error("Unknown envelope preset: " + preset);
                }
                valueNodes[id] = env;
            }
        }
```

- [ ] **Step 2: Build mforce_cli**

Run:
```
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_cli
```
Expected: success.

- [ ] **Step 3: Verify legacy patch still renders**

Run:
```
build/tools/mforce_cli/Release/mforce_cli.exe patches/SaveTest.json renders/envtask2_legacy.wav
```
Expected: success, wav written. (If `patches/SaveTest.json` doesn't use `Envelope`, pick any patch that does — search for `"type": "Envelope"` under `patches/`.)

- [ ] **Step 4: Verify a hand-written multi-stage patch loads**

Create `patches/_envtest_stages.json` with a minimal Envelope using `stages`:

```json
{
  "sampleRate": 48000,
  "graph": {
    "nodes": [
      { "id": "env1", "type": "Envelope",
        "params": { "stages": [
          {"percent":0.05, "startVal":0, "endVal":1, "type":"Linear", "power":0, "holdPct":0, "minSec":0.05, "maxSec":1.0},
          {"percent":0,    "startVal":1, "endVal":0, "type":"Sine",   "power":0, "holdPct":0, "minSec":0,    "maxSec":0}
        ] } },
      { "id": "sine1", "type": "SineSource", "params": { "frequency": 440.0 } },
      { "id": "ch1", "type": "SoundChannel", "inputs": { "source": "sine1" }, "params": { "volume": { "ref": "env1" }, "pan": 0.0 } },
      { "id": "mix1", "type": "StereoMixer", "inputs": { "channels": ["ch1"] } }
    ],
    "output": "mix1"
  },
  "instrument": { "polyphony": 1 },
  "seconds": 1.0,
  "score": [ { "note": 60, "velocity": 0.8, "time": 0.0, "duration": 0.8 } ]
}
```

Then run:
```
build/tools/mforce_cli/Release/mforce_cli.exe patches/_envtest_stages.json renders/envtask2_stages.wav
```
Expected: success, wav written. Quickly confirm with:
```
build/tools/wav_check/Release/wav_check.exe renders/envtask2_stages.wav
```
(Or just visually open the wav.) Should be a 1-second sine with a 50ms attack and a sine-shaped decay.

- [ ] **Step 5: Commit**

```
git add engine/src/patch_loader.cpp patches/_envtest_stages.json
git commit -m "feat(patch_loader): Envelope.params.stages multi-stage form (legacy preset preserved)"
```

---

### Task 3: UI — switch `NT_ENVELOPE` from ADSREnvelope to bare Envelope

**Files:**
- Modify: `tools/mforce_ui/main.cpp:227-230`

- [ ] **Step 1: Replace the ADSREnvelope hardcode**

In `tools/mforce_ui/main.cpp`, find:

```cpp
        if (typeName == NT_ENVELOPE) {
            dspSource = std::make_shared<ADSREnvelope>(DSP_SAMPLE_RATE);
            return;
        }
```

Replace with:

```cpp
        if (typeName == NT_ENVELOPE) {
            dspSource = std::make_shared<Envelope>(Envelope::make_adsr(DSP_SAMPLE_RATE,
                0.05f, 0.1f, 0.7f, 0.2f));
            return;
        }
```

- [ ] **Step 2: Build mforce_ui**

Run:
```
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_ui
```
Expected: success.

- [ ] **Step 3: Sanity-check launch (no crash)**

Run `build/tools/mforce_ui/Release/mforce_ui.exe`, verify the app starts and doesn't crash. Close immediately.

- [ ] **Step 4: Commit**

```
git add tools/mforce_ui/main.cpp
git commit -m "refactor(ui): NT_ENVELOPE constructs bare Envelope (4-stage ADSR shape)"
```

---

### Task 4: UI — stage table inspector

**Files:**
- Modify: `tools/mforce_ui/main.cpp` (insert after the FormantSpectrum block ending at line 3261)

- [ ] **Step 1: Add the stage table immediately after the FormantSpectrum block**

Locate the closing `}` of the `if (node->typeName == "FormantSpectrum" && !node->formantRows.empty())` block (currently ends around line 3261, just before the `// PatchOutput: polyphony` comment). Insert this block right after it:

```cpp
    // Inline stage table (bare Envelope)
    if (node->typeName == NT_ENVELOPE) {
        auto* env = dynamic_cast<Envelope*>(node->dspSource.get());
        if (env) {
            ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1), "Stages");

            const char* typeNames[] = { "Linear", "Expo", "InverseExpo", "Sine" };
            bool changed = false;
            int  removeIdx = -1;

            if (ImGui::BeginTable("stages", 9,
                    ImGuiTableFlags_SizingFixedFit | ImGuiTableFlags_BordersInnerV)) {
                ImGui::TableSetupColumn("pct");
                ImGui::TableSetupColumn("start");
                ImGui::TableSetupColumn("end");
                ImGui::TableSetupColumn("type");
                ImGui::TableSetupColumn("pow");
                ImGui::TableSetupColumn("hold");
                ImGui::TableSetupColumn("minS");
                ImGui::TableSetupColumn("maxS");
                ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();

                for (int i = 0; i < env->stage_count(); ++i) {
                    auto& s = env->stage(i);
                    ImGui::TableNextRow();
                    ImGui::PushID(i);

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##pct", &s.percent,        0.005f, 0.0f, 1.0f, "%.3f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##start", &s.ramp.startVal, 0.01f, -10.0f, 10.0f, "%.3f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##end",   &s.ramp.endVal,   0.01f, -10.0f, 10.0f, "%.3f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(90);
                    int curType = int(s.ramp.type);
                    if (ImGui::Combo("##type", &curType, typeNames, IM_ARRAYSIZE(typeNames))) {
                        s.ramp.type = RampType(curType); changed = true;
                    }
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##pow", &s.ramp.power,   0.05f, 0.0f, 10.0f, "%.2f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##hold", &s.ramp.holdPct, 0.005f, 0.0f, 1.0f, "%.3f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##min", &s.minSec, 0.005f, 0.0f, 99.0f, "%.3f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn(); ImGui::PushItemWidth(60);
                    changed |= ImGui::DragFloat("##max", &s.maxSec, 0.05f,  0.0f, 99.0f, "%.2f");
                    ImGui::PopItemWidth();

                    ImGui::TableNextColumn();
                    if (ImGui::SmallButton(" x ")) removeIdx = i;

                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            if (removeIdx >= 0) { env->remove_stage(removeIdx); changed = true; }
            if (ImGui::SmallButton(" + Add Stage ")) { env->add_stage_default(); changed = true; }

            if (changed) s_graphDirty = true;
        }
    }
```

- [ ] **Step 2: Build mforce_ui**

Run:
```
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_ui
```
Expected: success.

- [ ] **Step 3: Hand-test the inspector**

Launch `build/tools/mforce_ui/Release/mforce_ui.exe`. Add an Envelope node (Envelopes → Envelope). Open its properties panel. Verify:
- Stage table shows 4 rows (the make_adsr default)
- Columns in order: pct, start, end, type, pow, hold, minS, maxS, [x]
- DragFloat values change live; type Combo cycles Linear/Expo/InverseExpo/Sine
- `+ Add Stage` appends a row with start=prior endVal, end=0, percent=0.2, Linear
- `x` button removes a row
- Closing & re-opening the panel preserves edits

- [ ] **Step 4: Commit**

```
git add tools/mforce_ui/main.cpp
git commit -m "feat(ui): per-stage table inspector for bare Envelope"
```

---

### Task 5: UI — save path emits `stages`

**Files:**
- Modify: `tools/mforce_ui/main.cpp` (two parallel save sites: lines 1251-1255 and 1419-1422)

- [ ] **Step 1: Replace the first save block (around line 1251)**

Find:

```cpp
        // Envelope: add preset
        if (node.typeName == NT_ENVELOPE) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"]["preset"] = "adsr";
        }
```

Replace with:

```cpp
        if (node.typeName == NT_ENVELOPE) {
            if (auto* env = dynamic_cast<Envelope*>(node.dspSource.get())) {
                if (!jnode.contains("params")) jnode["params"] = json::object();
                json stages = json::array();
                for (int i = 0; i < env->stage_count(); ++i) {
                    const auto& s = env->stage(i);
                    const char* t = (s.ramp.type == RampType::Expo)        ? "Expo"
                                  : (s.ramp.type == RampType::InverseExpo) ? "InverseExpo"
                                  : (s.ramp.type == RampType::Sine)        ? "Sine"
                                                                            : "Linear";
                    stages.push_back({
                        {"percent",  s.percent},
                        {"startVal", s.ramp.startVal},
                        {"endVal",   s.ramp.endVal},
                        {"type",     t},
                        {"power",    s.ramp.power},
                        {"holdPct",  s.ramp.holdPct},
                        {"minSec",   s.minSec},
                        {"maxSec",   s.maxSec},
                    });
                }
                jnode["params"]["stages"] = stages;
            }
        }
```

- [ ] **Step 2: Replace the second save block (around line 1419) with the same code**

Find:

```cpp
        if (node.typeName == NT_ENVELOPE) {
            if (!jnode.contains("params")) jnode["params"] = json::object();
            jnode["params"]["preset"] = "adsr";
        }
```

Replace with the identical block from Step 1. (The two save paths must stay in sync; if a future refactor folds them into one, this duplication goes away.)

- [ ] **Step 3: Build mforce_ui**

Run:
```
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_ui
```
Expected: success.

- [ ] **Step 4: Hand-test save**

Launch the UI. Add an Envelope node. Edit a few stage values. Save the patch (e.g. `patches/_envtest_save.json`). Open the saved JSON in a text editor and verify it contains `"params": { "stages": [ ... ] }` with the values you edited (no `"preset": "adsr"`).

- [ ] **Step 5: Commit**

```
git add tools/mforce_ui/main.cpp
git commit -m "feat(ui): save Envelope as multi-stage params.stages"
```

---

### Task 6: UI — load path populates the inspector from `stages`

**Files:**
- Modify: `tools/mforce_ui/main.cpp` (around line 853, after the FormantSpectrum restore block, before the `// Restore config values` comment)

- [ ] **Step 1: Add Envelope-from-stages restore**

Inside the `if (jnode.contains("params"))` block in the loader's first pass, after the FormantSpectrum restore (around line 853, just before `// Restore config values`), add:

```cpp
            // Restore Envelope stages. For NT_ENVELOPE nodes saved with
            // params.stages, replace the live Envelope's stage list. (Legacy
            // preset-based Envelope JSON keeps loading via the patch loader's
            // make_ar / make_adsr fallback — the inspector will then show the
            // resulting 2- or 4-stage shape.)
            if (gn.typeName == NT_ENVELOPE && params.contains("stages")) {
                if (auto* env = dynamic_cast<Envelope*>(gn.dspSource.get())) {
                    *env = Envelope(DSP_SAMPLE_RATE);
                    for (const auto& sj : params["stages"]) {
                        Envelope::Stage s;
                        s.ramp.startVal = sj.value("startVal", 0.0f);
                        s.ramp.endVal   = sj.value("endVal",   0.0f);
                        s.ramp.power    = sj.value("power",    0.0f);
                        s.ramp.holdPct  = sj.value("holdPct",  0.0f);
                        std::string t   = sj.value("type", std::string("Linear"));
                        s.ramp.type = (t == "Expo")        ? RampType::Expo
                                    : (t == "InverseExpo") ? RampType::InverseExpo
                                    : (t == "Sine")        ? RampType::Sine
                                                            : RampType::Linear;
                        s.percent = sj.value("percent", 0.0f);
                        s.minSec  = sj.value("minSec",  0.0f);
                        s.maxSec  = sj.value("maxSec",  0.0f);
                        env->add_stage(s);
                    }
                }
            }
```

- [ ] **Step 2: Build mforce_ui**

Run:
```
"C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_ui
```
Expected: success.

- [ ] **Step 3: Round-trip test in the UI**

Open the file from Task 5 Step 4 (`patches/_envtest_save.json`) in mforce_ui. Confirm the Envelope inspector shows the exact stages you saved. Edit one value, save, reopen, confirm again.

- [ ] **Step 4: Backward-compat check**

Find an existing patch that uses `"type": "Envelope"` with `"preset": "adsr"` (or hand-write one). Open it in the UI. Confirm it loads without error and the inspector shows the 4 stages produced by `make_adsr`. Save → reload → confirm the saved file now contains `params.stages` (the migration is implicit on save).

- [ ] **Step 5: Commit**

```
git add tools/mforce_ui/main.cpp
git commit -m "feat(ui): load Envelope from params.stages into live inspector"
```

---

### Task 7: End-to-end render verification

- [ ] **Step 1: Render a multi-stage Envelope patch via CLI**

Run:
```
build/tools/mforce_cli/Release/mforce_cli.exe patches/_envtest_stages.json renders/envtask7_stages.wav
```
Expected: success.

- [ ] **Step 2: Render an equivalent legacy preset patch**

Hand-write or copy `patches/_envtest_preset.json` with the same shape but using `"preset": "adsr"` form, then render:

```json
{
  "sampleRate": 48000,
  "graph": {
    "nodes": [
      { "id": "env1", "type": "Envelope",
        "params": { "preset": "adsr", "attack": 0.05, "decay": 0.0,
                    "sustainLevel": 1.0, "release": 0.0 } },
      { "id": "sine1", "type": "SineSource", "params": { "frequency": 440.0 } },
      { "id": "ch1", "type": "SoundChannel", "inputs": { "source": "sine1" }, "params": { "volume": { "ref": "env1" }, "pan": 0.0 } },
      { "id": "mix1", "type": "StereoMixer", "inputs": { "channels": ["ch1"] } }
    ],
    "output": "mix1"
  },
  "instrument": { "polyphony": 1 },
  "seconds": 1.0,
  "score": [ { "note": 60, "velocity": 0.8, "time": 0.0, "duration": 0.8 } ]
}
```

```
build/tools/mforce_cli/Release/mforce_cli.exe patches/_envtest_preset.json renders/envtask7_preset.wav
```
Expected: success. Both wavs should sound equivalent (a quick A/B in any wav viewer / player confirms shape).

- [ ] **Step 3: Render an existing real patch with an Envelope (regression check)**

Pick a patch under `patches/` that uses an Envelope (preset or otherwise) — `SaveTest.json` is a good candidate if it qualifies. Render before/after this branch and confirm output is unchanged.

- [ ] **Step 4: Copy renders to main repo renders/ dir if running in a worktree**

Per persistent feedback, copy any worktree renders to `C:/@dev/repos/mforce/renders/`.

- [ ] **Step 5: Commit any new test patches**

If `_envtest_preset.json` was added in this task:
```
git add patches/_envtest_preset.json
git commit -m "test(envelope): legacy preset vs multi-stage equivalence patch"
```

---

## Self-review checklist

- [x] Spec coverage: every section in `2026-04-28-envelope-node-design.md` maps to a task (engine→T1, patch loader→T2, UI init_dsp→T3, inspector→T4, save→T5, load→T6, testing→T7).
- [x] No placeholders — every step has exact code or exact commands.
- [x] Type consistency: `Envelope`, `Envelope::Stage`, `RampType`, `add_stage`, `add_stage_default`, `remove_stage`, `stage_count`, `stage(i)` all match between header and call sites.
- [x] Frequent commits — one per task.

## Execution

Plan complete and saved to `docs/superpowers/plans/2026-04-28-envelope-node.md`.

Two execution options:
1. **Subagent-Driven (recommended)** — fresh subagent per task, review between tasks.
2. **Inline Execution** — execute tasks in this session with checkpoints.

Per memory: for sequential refactors where I already have codebase context, inline is ~10x faster. Recommend Inline unless you want isolation.
