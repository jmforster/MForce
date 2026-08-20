# PerformSource P2a Implementation Plan — the wiring format

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

> **Revision 2 — 2026-08-19.** Supersedes the version parked earlier the same
> day. That one decided where a chain landing on a config gets written before
> the model existed; `docs/pin_model_design.md` now settles the model, and this
> revision follows it. Its engine content survives; the vocabulary and the
> serialization changed. The old cost estimate (~40 configurators needing
> teaching) was wrong and is gone.

---

## Read this part first (plain language)

### What's true today

A patch file has a block called `paramMap` — the list of "when a note plays,
send its pitch to *this* setting on *that* node," sometimes bent through a
curve first.

P1 (landed 2026-08-18) changed what happens to that block at load. It's now
turned into ordinary graph parts — a node reporting the note's pitch, a curve
node, a wire. Nothing sounds different; all 196 patches render byte for byte
identical.

### What's missing

Those parts only exist in memory. There's no way to *spell* "a node that
reports the note's pitch" in patch JSON — the loader invents it at load and it
dies when the patch unloads. The next step (P2b) teaches the UI's Save to write
this, and Save has to write something.

### What P2a does

Defines the spelling, and proves it says the same thing the old one did.

1. **Renames `config` to `setting`** throughout the engine, and lands it first
   so everything after is written in the vocabulary we're keeping. Zero patch
   files and zero on-screen text change — the format has no `config` key and
   the UI already says "Settings."
2. A new node type — **`PerformNode`** — that reports the note's pitch or
   velocity. As many per patch as you like; they all report the same note.
3. Teaches the loader to read **`dynamicPins`**, a small per-node object naming
   the settings this patch drives per note.
4. A converter that rewrites any patch's `paramMap` into the new spelling.
5. The proof: run all 196 patches through the converter, render them, require
   every one **byte for byte identical** to what it renders today.

### What it costs you

Nothing changes in how patches load or sound. `paramMap` keeps working and
keeps being read. Your patch files aren't touched — the converter writes to a
scratch copy. Files only change format when the UI saves them, which is P2b.
Two new baseline patches land in `patches/baselines/perform/`.

### The one decision left in here

`pin_model_design.md` §11 says a dynamic pin serialises separately from
`params`, and gives the reason: it's a different kind of pin, and node
*construction* reads `params` before it knows which keys are settings. It
doesn't name the JSON key. **This plan calls it `dynamicPins`.** One line to
change if you'd rather it were something else — say so before Task 3.

```json
{ "id": "env", "type": "Envelope",
  "params":      { "preset": "adsr", "sustainLevel": 0.7 },
  "dynamicPins": { "sustainLevel": { "ref": "__curve_sus" } } }
```

### How you'll know it worked

`python tools/null_gate_wiring.py` prints `196/196 identical after conversion`.
Anything else means the format doesn't say what `paramMap` said, and the plan
stops rather than papering over it.

---

## The rest is execution detail

**Goal:** Give the bindings P1 builds in memory a serialized form — the
`setting` rename, a `PerformNode` JSON type, `dynamicPins`, and a
paramMap→wiring converter — proven by rendering all 196 gated patches from
their *converted* files against the frozen manifest.

**Architecture:** The voice's `PerformSource` and its two `PerformOut` adapters
are created **before** `build_graph` and handed down as a context, so a
`{"type":"PerformNode","params":{"field":"frequency"}}` node resolves to the
shared adapter — one JSON node per field, because each `PerformOut` already is
a separate one-output `ValueSource`. Dynamic pins are read from a per-node
`dynamicPins` object and become Setup-evaluated push bindings. Legacy
`paramMap` stays untouched and is read forever; the two formats coexist.

**Not in P2a:** all UI work — PerformNode/CurveNode as editor nodes, the knot
editor, grey→gold promotion, Curves/Mappings as derived views, the UI
serializer. That is P2b.

**Tech Stack:** C++20 (MSVC), CMake (VS-bundled:
`C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`),
nlohmann::json, Python 3.

## Global Constraints

- **The null gate is the law.** `python tools/null_gate_perform_source.py` must
  stay 196/196 after every task. A diff = stop and diagnose, never rationalize.
- **A running `mforce_ui.exe` is not a reason to change the plan.** If the exe
  is locked, rename it out of the way and link the new one (run-19 precedent —
  `mforce_ui_running_backup2.exe` in the build dir is that trick's residue);
  the running process keeps going off the renamed file and picks up the new
  build on its next restart. If the rename fails, say so and ask Matt to close
  it — do not silently reshape the work around it.
- No heap allocation in `next()` paths (CLAUDE.md non-negotiable).
- Verify the current branch live (`git branch --show-current`) before each
  commit — two Claudes share this working copy.
- Stage with explicit paths. `git add -u` would sweep up
  `engine/include/mforce/render/patch_loader.h` and
  `tools/mforce_ui/CMakeLists.txt`, both dirty before this work started and
  not ours to commit.
- Commit trailer: `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`
- New JSON keys are optional with defaults reproducing current behavior.
- `__`-prefixed node ids are reserved for synthesized nodes.

## Known bit-identity risks (the gate decides; do not pre-emptively "fix")

1. **RefSource wrapping — HIT AND RESOLVED in Task 3.** `build_bindings` calls
   `set_param` directly, so a chain feeding N pins is shared raw. The generic
   param pass goes through `resolve_param`, which wraps 2nd+ uses in
   `RefSource` (`engine/src/patch_loader.cpp:286-295`).

   This is **not** a corner case in wiring format: one `__perf_freq` normally
   feeds many pins (131 frequency targets across the library), so the wrapper
   is the common path, not the exception.

   It broke immediately. `RefSource::next()` returns `source->current()`
   **without** pulling the source — it assumes the primary consumer already
   did this sample. `PerformOut::current()` returned a cached `cur_` written
   only by `next()`, so every wrapped copy reported 0 until the primary
   happened to pull. First symptom: `ERROR: WaveSource: non-positive
   frequency` the moment a PerformNode fed two consumers.

   **Fix applied — make `PerformOut` stateless**, not exempt it from usage
   counting as an earlier draft of this plan suggested. It is a pure view over
   `NoteState`, constant between `set_note` calls, so there was never anything
   to cache; `next()` and `current()` both read live. This fixes the root
   cause, keeps `RefSource` working as designed, and adds no special case to
   the loader. Bit-identical for P1's direct (unwrapped) uses, since the value
   only changes at `set_note`.

   `CurveNode` needs no equivalent change: its `next()` does pull its source,
   so a wrapped copy's `current()` is valid once the primary has run.
2. **Multiplex clones.** P1 pushes into clones (`set_clone_param`); wiring
   format instead pulls, because `extract_subgraph_json` drags the referenced
   chain into each clone's subtree and each clone builds its own CurveNode
   against the shared adapter. Values are equal; the 7 Multiplex patches are
   the proof. Two things to check first: `extract_subgraph_json` follows refs in
   `params` and must learn to follow `dynamicPins` refs too, and `bind_wiring`
   walks only top-level `nodeMap`, so a dynamic pin *inside* a clone subtree
   isn't seen. If either bites, fall back to matching P1 exactly: detect
   `vg.topMultiplex` in `bind_wiring` and emit a `PushBinding` with
   `targetNodeId` set.
3. **Bend graft.** P1 populates `vg.bendSwaps` for exactly the entries with an
   empty freq-curve. Task 4 reproduces that membership by graph shape, or the
   4 bend/slide baselines diff.

---

### Task 0: Confirm the baseline is green

**Files:** none (verification only).

- [ ] **Step 1: Handle a locked UI exe**

Run: `Get-Process mforce_ui -ErrorAction SilentlyContinue`

If it prints a process:
```
Rename-Item build/tools/mforce_ui/Release/mforce_ui.exe mforce_ui_locked.exe
```
If the rename fails, stop and ask Matt to close it. Either way, note in the run
report that the running UI is one build behind until he restarts it.

- [ ] **Step 2: Verify branch and note pre-existing dirt**

Run: `git branch --show-current` then `git status --short`
Expected: `main`. `engine/include/mforce/render/patch_loader.h` and
`tools/mforce_ui/CMakeLists.txt` are expected dirty and are NOT ours.

- [ ] **Step 3: Build and gate**

Run:
```
& "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
```
then `python tools/null_gate_perform_source.py`
Expected: `196/196 identical`, exit 0. If not, STOP — something landed dirty.

---

### Task 1: Rename `config` → `setting`

**Files:**
- Modify: `engine/include/mforce/core/dsp_value_source.h` (the declarations)
- Modify: 26 further files under `engine/` and `tools/` (mechanical)
- Modify: `tools/mforce_cli/main.cpp` (`--dump-descriptors` output key)

**Interfaces:**
- Produces: `SettingType`, `SettingDescriptor`, `setting_descriptors()`,
  `set_setting()`, `get_setting()`. Every later task uses these names.
- Produces: `--dump-descriptors` emits `"settings"` where it emitted
  `"configs"`. Task 5's converter consumes the new key.

**Why first:** approved by Matt 2026-08-19 (`pin_model_design.md` §8). Landing
it before the rest means Tasks 2-7 are written once, in the vocabulary we're
keeping, instead of being written in the old one and renamed after.

**Why it is safe:** the word reaches no patch file (the format has no `config`
key — settings are read out of the same `params` object) and no on-screen text
(the Properties panel already says "Settings"). No rendered audio changes, so
the null gate proves the rename exactly.

- [ ] **Step 1: Rename the declarations**

In `engine/include/mforce/core/dsp_value_source.h`:

```cpp
enum class SettingType { Float, Int, Bool };

struct SettingDescriptor {
  const char* name;
  SettingType type;
  float default_value;
  float min_value;
  float max_value;   // for Float/Int; ignored for Bool
  const char* const* enum_labels = nullptr;
};
```

and on `ValueSource`:

```cpp
  virtual std::span<const SettingDescriptor> setting_descriptors() const { return {}; }
  virtual void set_setting(std::string_view /*name*/, float /*value*/) {}
  virtual float get_setting(std::string_view /*name*/) const { return 0.0f; }
```

Keep the existing signatures' semantics identical — this step changes spelling
only. Match the real `get_config` signature before writing `get_setting`; read
it rather than trusting the line above.

- [ ] **Step 2: Sweep the rest**

27 files reference the old names. Rename, in this order, checking each compiles
before moving on:

`ConfigType`→`SettingType`, `ConfigDescriptor`→`SettingDescriptor`,
`config_descriptors`→`setting_descriptors`, `set_config`→`set_setting`,
`get_config`→`get_setting`, and the UI's `configValues`→`settingValues`.

Do NOT rename: `MConfig` in the legacy sibling repo (out of tree), the
`isConfig` field on `PitchedInstrument::PushBinding` (Task 3 renames it to
`isSetting` as part of its own edit), or any local variable named `cfg` that
does not refer to this concept — check before touching.

Heaviest files, expect the most hits:
`engine/include/mforce/source/additive/partials.h` (88),
`engine/include/mforce/source/wave_evolution.h` (78),
`engine/include/mforce/core/envelope_presets.h` (54),
`tools/mforce_ui/main.cpp` (21).

- [ ] **Step 3: Rename the dump key**

In `tools/mforce_cli/main.cpp`'s `run_dump_descriptors`, emit `"settings"`
instead of `"configs"` (the array of `{name,type,isEnum,default,min,max}`
objects). Leave `inputs`/`params`/`arrays` alone.

- [ ] **Step 4: Build everything**

Run:
```
& "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
```
Expected: `mforce_cli`, `mforce_ui`, `engine_tests` all build. A missed rename
is a compile error, not a silent bug — that is the point of doing it wholesale.

- [ ] **Step 5: Prove nothing moved**

Run: `python tools/null_gate_perform_source.py`
Expected: `196/196 identical`. A pure rename that changes a hash means
something was renamed that shouldn't have been.

Also run `./build/tools/engine_tests/Release/engine_tests.exe` — expected
`0 failures`.

- [ ] **Step 6: Confirm the word is gone AND nothing was mangled**

Run: `grep -rn "ConfigType\|ConfigDescriptor\|config_descriptors\|set_config\|get_config" engine tools --include=*.h --include=*.cpp`
Expected: no output.

**Then the check that actually matters.** A blanket substitution can produce a
*consistently* wrong name, which compiles clean — `get_config` is a prefix of
`get_configurator`, and a naive sed turns it into `get_settingurator` across
declaration, definition and call site with no compile error. Enumerate every
occurrence of each renamed token and confirm the set is exactly the six
intended names:

```
grep -rno "set_setting[A-Za-z_]*\|get_setting[A-Za-z_]*\|setting_descriptors[A-Za-z_]*\|SettingType[A-Za-z_]*\|SettingDescriptor[A-Za-z_]*\|settingValues[A-Za-z_]*" --include=*.h --include=*.cpp engine tools | sed 's/.*://' | sort -u
```
Expected exactly: `SettingDescriptor`, `SettingDescriptors`, `SettingType`,
`get_setting`, `set_setting`, `settingValues`, `setting_descriptors`. Anything
longer is a prefix collision — fix it and re-run.

**`configurator` keeps its name.** It configures a node from JSON — pins,
settings and arrays alike — so it is not a setting concept. Protect it during
any sed: `s/configurator/@@CFGR@@/g; …; s/@@CFGR@@/configurator/g`.

- [ ] **Step 6b: Rename the strings the identifier sweep cannot reach**

Six user-facing strings and one internal JSON key. In `tools/mforce_ui/main.cpp`:
the two `"  (config)"` target annotations (`:4775`, `:4970`), the two
`"Param / config"` combo labels, the two `"…no mappable/curve-able
params/configs"` empty-state lines, and the node-clipboard key `j["configs"]`
(`:6818` write, `:6865` read). The clipboard is process-local — "no OS
clipboard involvement" (`:6783`) — so renaming its key has no compatibility
cost. Also do a prose-comment pass over `dsp_value_source.h`, `instrument.h`
and `patch_loader.cpp`, which is where a reader meets the vocabulary first.

- [ ] **Step 7: Commit**

```bash
git add -A engine tools && git reset engine/include/mforce/render/patch_loader.h tools/mforce_ui/CMakeLists.txt
git commit -m "refactor(engine): config -> setting, the name the UI already used"
```

---

### Task 2: `PerformNode` JSON type

**Files:**
- Modify: `engine/src/patch_loader.cpp`
- Modify: `tools/engine_tests/main.cpp`
- Create: `patches/baselines/perform/wiring_smoke.json`

**Interfaces:**
- Produces: file-local `struct PerformContext { std::shared_ptr<ValueSource> freqOut, velOut; };`
- Produces: `build_graph(nodeMap, nodeOrder, sampleRate, const PerformContext* perf = nullptr)`
- Produces: JSON type `PerformNode`, one param `field` (`"frequency"` |
  `"velocity"`, default `"frequency"`). Task 5's converter emits exactly this.

- [ ] **Step 1: Add the context and thread it through**

Above the forward declarations in `engine/src/patch_loader.cpp` (~line 257):

```cpp
// Per-voice performance adapters, handed down so PerformNode instances all
// resolve to the SAME objects (pin_model_design.md; perform_source_design.md
// §2.3: "All instances render the same outputs"). Null in NodeGraph/standalone
// contexts, where a PerformNode is an authoring error rather than a silent
// constant.
struct PerformContext {
    std::shared_ptr<ValueSource> freqOut, velOut;
};
```

Add the parameter to `build_graph`'s declaration and definition, and to
`build_subgraph_with_seed_perturbation` (declaration ~266, definition ~696),
forwarding `perf` to the `build_graph` call inside it.

**The Multiplex closure is a lifetime trap.** An earlier draft of this plan
said it "runs during that same `build_graph` call," so a raw pointer would be
safe. **That is wrong.** `mux->set_template(...)` *stores* the builder;
`MultiplexSource::rebuild_()` invokes it from `prepare()`
(`multiplex_source.h:106`), and `mark_dirty()` can retrigger it later after a
UI edit. A `const PerformContext*` into the voice-loop stack frame would
dangle, and a UI edit could retrigger the use-after-free at will.

So `PerformContext` holds `shared_ptr`s, and the closure captures a **copy by
value**:

```cpp
                    int sr = sampleRate;
                    const bool hasPerf = (perf != nullptr);
                    PerformContext perfCopy = hasPerf ? *perf : PerformContext{};
                    MultiplexSource::InstanceBuilder builder =
                        [subtreeStr, baseSeed, sr, hasPerf, perfCopy](int instanceIdx) {
                            uint32_t perturbation =
                                baseSeed ^ (uint32_t(instanceIdx) * 0x9E3779B9u);
                            return build_subgraph_with_seed_perturbation(
                                subtreeStr, perturbation, sr,
                                hasPerf ? &perfCopy : nullptr);
                        };
```

The copy holds `shared_ptr`s to the very same adapters, so clones still share
the voice's one `PerformSource` — the property the Multiplex bit-identity risk
depends on.

- [ ] **Step 2: Add the dispatch branch**

Directly after the `CurveNode` branch (ends ~line 388):

```cpp
else if (type == "PerformNode") {
    // Leaf: resolves to the voice's shared PerformOut adapter. Multiple
    // PerformNode nodes with the same field are the SAME object by design.
    if (!perf)
        throw std::runtime_error("PerformNode '" + id + "': no voice context "
            "(PerformNode is instrument/patch mode only — §6.4)");
    const std::string field =
        pp ? pp->value("field", std::string("frequency")) : std::string("frequency");
    if      (field == "frequency") valueNodes[id] = perf->freqOut;
    else if (field == "velocity")  valueNodes[id] = perf->velOut;
    else throw std::runtime_error("PerformNode '" + id + "': unknown field '" +
                                  field + "' (expected frequency|velocity)");
}
```

- [ ] **Step 3: Create the adapters before build_graph at both call sites**

Reduce `attach_perform_source` (~line 884) to the conversion step:

```cpp
// Convert the legacy paramMap, if the patch carries one. The performance
// objects are now created BEFORE build_graph (PerformNode needs them during
// graph construction) — see the two voice loops.
static void attach_perform_source(const json& instJson, const GraphResult& g,
                                  PitchedInstrument::VoiceGraph& vg)
{
    if (instJson.contains("paramMap"))
        build_bindings(instJson["paramMap"], g, vg);
}
```

In **both** voice loops — `load_patch_file`'s instrument path (~line 946) and
`load_instrument_patch`'s (~line 1141) — hoist construction:

```cpp
    for (int v = 0; v < polyphony; ++v) {
        PitchedInstrument::VoiceGraph vg;
        vg.performSource = std::make_shared<PerformSource>();
        vg.freqOut = std::make_shared<PerformOut>(vg.performSource,
                                                  PerformOut::Field::Frequency);
        vg.velOut  = std::make_shared<PerformOut>(vg.performSource,
                                                  PerformOut::Field::Velocity);
        PerformContext perf{vg.freqOut, vg.velOut};

        auto g = build_graph(nodeMap, nodeOrder, sampleRate, &perf);

        auto srcIt = g.valueNodes.find(outputId);
        if (srcIt == g.valueNodes.end())
            throw std::runtime_error("instrument: output node '" + outputId + "' not found");
        vg.source = srcIt->second;
        vg.topMultiplex = std::dynamic_pointer_cast<MultiplexSource>(vg.source);

        attach_perform_source(instJson, g, vg);

        inst->voicePool.push_back(std::move(vg));
    }
```

Preserve whatever else each loop already does between `build_graph` and
`voicePool.push_back`.

- [ ] **Step 4: Build and gate**

Build, then `python tools/null_gate_perform_source.py`.
Expected: `196/196 identical`. No patch uses `PerformNode` yet, so any diff
here is a pure-refactor regression.

- [ ] **Step 5: Engine test**

In `tools/engine_tests/main.cpp`, after `run_curve_node_tests`:

```cpp
#include "mforce/render/perform_source.h"

static void run_perform_node_tests() {
    auto ps = std::make_shared<PerformSource>();
    auto f = std::make_shared<PerformOut>(ps, PerformOut::Field::Frequency);
    auto v = std::make_shared<PerformOut>(ps, PerformOut::Field::Velocity);

    ps->set_note(220.0f, 0.25f, 4800);
    f->next(); v->next();
    CHECK_NEAR(f->current(), 220.0f, 1e-6f);
    CHECK_NEAR(v->current(), 0.25f,  1e-6f);

    // Idempotent between set_note calls.
    for (int i = 0; i < 8; ++i) f->next();
    CHECK_NEAR(f->current(), 220.0f, 1e-6f);

    // A CurveNode on the shared adapter re-evaluates on the next note.
    CurveNode cn; cn.interp = CurveNode::CurveInterp::LogX;
    cn.knots = {{110.0f, 0.0f}, {880.0f, 3.0f}};
    cn.set_param("source", f);
    cn.next();
    CHECK_NEAR(cn.current(), 1.0f, 1e-4f);   // 220 is one octave of three
    ps->set_note(880.0f, 0.25f, 4800);
    cn.next();
    CHECK_NEAR(cn.current(), 3.0f, 1e-4f);
}
```

Call it from `main`. Build `engine_tests`, run it, expect `0 failures`.

- [ ] **Step 6: The smoke patch**

Create `patches/baselines/perform/wiring_smoke.json`. Names verified against
the registry: `SineSource` is registered and inherits `frequency`/`amplitude`/
`phase` from `WaveSource` (`dsp_wave_source.h:75-81`); the adsr preset's
sustain key is `sustainLevel`, not `sustain` (`envelope_json.h:41`) — a wrong
key there is silently defaulted, not reported.

```json
{
  "sampleRate": 48000,
  "nodes": [
    { "id": "__perf_freq", "type": "PerformNode", "params": { "field": "frequency" } },
    { "id": "__curve_bright", "type": "CurveNode",
      "params": { "interp": "logx",
                  "knots": [[110.0, 0.9], [880.0, 0.2]],
                  "source": { "ref": "__perf_freq" } } },
    { "id": "tone", "type": "SineSource",
      "params": { "frequency": { "ref": "__perf_freq" }, "amplitude": 1.0 } },
    { "id": "env", "type": "Envelope",
      "params": { "preset": "adsr", "attack": 0.01, "decay": 0.1,
                  "sustainLevel": 0.7, "release": 0.2 } },
    { "id": "vca", "type": "CombinedSource",
      "params": { "source1": { "ref": "tone" }, "source2": { "ref": "env" },
                  "operation": "multiply", "gainAdj": 0.0 } },
    { "id": "out", "type": "CombinedSource",
      "params": { "source1": { "ref": "vca" }, "source2": { "ref": "__curve_bright" },
                  "operation": "multiply", "gainAdj": 0.0 } }
  ],
  "output": "out",
  "instrument": { "polyphony": 4, "volume": 1.0 },
  "score": [ { "note": 57, "start": 0.0, "duration": 0.8, "velocity": 0.8 },
             { "note": 81, "start": 1.0, "duration": 0.8, "velocity": 0.8 } ],
  "seconds": 2.0
}
```

- [ ] **Step 7: Render and prove the curve fires**

Run: `./build/tools/mforce_cli/Release/mforce_cli.exe patches/baselines/perform/wiring_smoke.json renders/scratch/wiring_smoke.wav`
Expected: exit 0, no `[loader]` warnings.

The A5 note must be quieter than A2 — `__curve_bright` maps 110→0.9, 880→0.2:

```
python -c "import wave,struct,sys; w=wave.open('renders/scratch/wiring_smoke.wav'); n=w.getnframes(); sr=w.getframerate(); ch=w.getnchannels(); d=struct.unpack('<%dh'%(n*ch), w.readframes(n)); seg=lambda a,b: max(abs(x) for x in d[int(a*sr)*ch:int(b*sr)*ch]); lo=seg(0.0,0.8); hi=seg(1.0,1.8); print('A2',lo,'A5',hi,'ratio',hi/lo); sys.exit(0 if hi < lo*0.6 else 1)"
```
Expected: ratio well under 0.6, exit 0. Equal peaks = the CurveNode isn't
wired; diagnose before continuing.

- [ ] **Step 8: Prove the NodeGraph guard fires**

```
python -c "import json; d=json.load(open('patches/baselines/perform/wiring_smoke.json')); d.pop('instrument'); json.dump(d, open('renders/scratch/no_inst.json','w'), indent=1)"
```
Run the CLI on it. Expected: non-zero exit, message `PerformNode '__perf_freq':
no voice context`. Silent success means the guard isn't on the NodeGraph path —
find the other `build_graph` call site.

- [ ] **Step 9: Commit**

```bash
git add engine/src/patch_loader.cpp tools/engine_tests/main.cpp patches/baselines/perform/wiring_smoke.json
git commit -m "feat(engine): PerformNode JSON type — voice adapters as graph citizens"
```

---

### Task 3: `dynamicPins` — settings driven per note

**Files:**
- Modify: `engine/src/patch_loader.cpp` (new `bind_wiring` pass + call sites)
- Modify: `engine/include/mforce/render/instrument.h` (`isConfig`→`isSetting`)
- Create: `patches/baselines/perform/wiring_setting.json`

**Interfaces:**
- Consumes: `PerformContext`, `build_graph(…, perf)` from Task 2.
- Produces: `static void bind_wiring(const std::unordered_map<std::string, json>& nodeMap,
  const std::vector<std::string>& nodeOrder, const GraphResult& g,
  PitchedInstrument::VoiceGraph& vg);` — Task 4 extends this same function.

**Why:** nothing reads `dynamicPins` today, so a converted patch would silently
lose every setting target. 118 of the 196 gated patches carry a paramMap and
their targets include real settings — `Envelope.sustainLevel` ×4 and the KS
piano's `t60`/`brightness`/`dispersion`/`inharmGain`/`detune` across 85.

- [ ] **Step 1: Write the failing patch**

Create `patches/baselines/perform/wiring_setting.json`. It drives an Envelope's
`sustainLevel` **setting** from note pitch through a curve — low notes sustain,
high notes don't. `sustainLevel` appears twice and that is correct: the scalar
in `params` is what construction reads, the ref in `dynamicPins` is what
overrides it per note.

**Patch-shape rules learned in Task 2 — get these wrong and the failure is
silent or misleading:**
- Instrument patches nest the graph under **`"graph"`**; both instrument load
  paths do `root.at("graph").at("nodes")` (`patch_loader.cpp:965`, `:1169`).
  Top-level `"nodes"` is NodeGraph-mode only and throws here.
- A score note's start time key is **`"time"`**, not `"start"`
  (`patch_loader.cpp:1026`). A wrong key is silently 0.0, so every note stacks
  at the origin.
- **`"seconds"` is inert** on the instrument path — render length comes from
  the score's last `time + duration` (`patch_loader.cpp:1052`). Don't include
  it; a key that does nothing is the failure class this project keeps hitting.
- Measured DC/peak values carry a **mono→stereo −3 dB factor** (×0.7071,
  backlog 3d). Divide it out before comparing against theory.

```json
{
  "sampleRate": 48000,
  "graph": {
    "nodes": [
      { "id": "__perf_freq", "type": "PerformNode", "params": { "field": "frequency" } },
      { "id": "__curve_sus", "type": "CurveNode",
        "params": { "interp": "logx",
                    "knots": [[110.0, 0.9], [880.0, 0.05]],
                    "source": { "ref": "__perf_freq" } } },
      { "id": "tone", "type": "SineSource",
        "params": { "frequency": { "ref": "__perf_freq" }, "amplitude": 1.0 } },
      { "id": "env", "type": "Envelope",
        "params": { "preset": "adsr", "attack": 0.01, "decay": 0.05,
                    "sustainLevel": 0.7, "release": 0.1 },
        "dynamicPins": { "sustainLevel": { "ref": "__curve_sus" } } },
      { "id": "out", "type": "CombinedSource",
        "params": { "source1": { "ref": "tone" }, "source2": { "ref": "env" },
                    "operation": "multiply", "gainAdj": 0.0 } }
    ],
    "output": "out"
  },
  "instrument": { "polyphony": 4, "volume": 1.0 },
  "score": [ { "note": 45, "time": 0.0, "duration": 0.8, "velocity": 0.8 },
             { "note": 81, "time": 1.0, "duration": 0.8, "velocity": 0.8 } ]
}
```

- [ ] **Step 2: Watch it do nothing**

Render it, then measure each note's sustain plateau:

```
python -c "import wave,struct; w=wave.open('renders/scratch/wiring_setting.wav'); n=w.getnframes(); sr=w.getframerate(); ch=w.getnchannels(); d=struct.unpack('<%dh'%(n*ch), w.readframes(n)); seg=lambda a,b: max(abs(x) for x in d[int(a*sr)*ch:int(b*sr)*ch]); print('A2 sustain',seg(0.4,0.7),'A5 sustain',seg(1.4,1.7))"
```
Expected NOW: roughly equal — `dynamicPins` is ignored and both notes used the
patch's own `sustainLevel` 0.7. This is the failing test.

- [ ] **Step 3: Rename the PushBinding flag**

In `engine/include/mforce/render/instrument.h`, rename `PushBinding::isConfig`
to `isSetting` and update its uses in `apply_note_bindings` (the
`b.consumer->set_setting(...)` branch) and in `build_bindings`.

- [ ] **Step 4: Add the bind_wiring pass**

After `build_bindings` ends (~line 880) in `engine/src/patch_loader.cpp`:

```cpp
// ---------------------------------------------------------------------------
// Wiring-format bindings (pin_model_design.md §3, §11). The generic param pass
// already wires refs on ordinary (fixed) pins; this pass handles DYNAMIC pins:
// a node's "dynamicPins" object, naming settings this patch drives per note.
// Settings rebuild state and have no pointer to pull, so the chain is
// evaluated ONCE per note and pushed via set_setting. They live outside
// "params" because construction reads params before any descriptor is known,
// and nlohmann's value() throws on an object where it wants a number.
// Task 4 adds bend-swap classification to this same walk.
// ---------------------------------------------------------------------------
static void bind_wiring(const std::unordered_map<std::string, json>& nodeMap,
                        const std::vector<std::string>& nodeOrder,
                        const GraphResult& g,
                        PitchedInstrument::VoiceGraph& vg)
{
    for (const auto& id : nodeOrder) {
        const auto& node = nodeMap.at(id);
        auto nodeIt = g.valueNodes.find(id);
        if (nodeIt == g.valueNodes.end()) continue;
        const auto& consumer = nodeIt->second;

        if (node.contains("dynamicPins")) {
            for (const auto& [key, v] : node["dynamicPins"].items()) {
                if (!v.is_object() || !v.contains("ref"))
                    throw std::runtime_error("wiring: '" + id + ".dynamicPins." +
                                             key + "' must be a {\"ref\": …} object");
                bool isSetting = false;
                for (const auto& desc : consumer->setting_descriptors())
                    if (key == desc.name) { isSetting = true; break; }
                if (!isSetting)
                    throw std::runtime_error("wiring: '" + id + ".dynamicPins." +
                        key + "' is not a setting on " + consumer->type_name() +
                        " (fixed pins belong in \"params\")");

                const std::string srcId = v.at("ref").get<std::string>();
                auto srcIt = g.valueNodes.find(srcId);
                if (srcIt == g.valueNodes.end())
                    throw std::runtime_error("wiring: '" + id + "." + key +
                                             "' refs unknown node '" + srcId + "'");

                PitchedInstrument::PushBinding b;
                b.consumer     = consumer;
                b.paramName    = key;
                b.targetNodeId = id;
                b.chain        = srcIt->second;
                b.cs           = nullptr;
                b.isSetting    = true;
                vg.pushBindings.push_back(std::move(b));
            }
        }
    }
}
```

- [ ] **Step 5: Call it from both voice loops**

After `attach_perform_source(instJson, g, vg);` in both loops:

```cpp
        bind_wiring(nodeMap, nodeOrder, g, vg);
```

Order is deliberate: legacy `build_bindings` runs first, so a patch carrying
both formats resolves its legacy entries exactly as today and the wiring pass
appends. No gated patch carries both; the ordering is documented so a hybrid,
if one ever appears, is deterministic.

- [ ] **Step 6: Rebuild and re-measure**

Re-render and re-run the Step 2 measurement.
Expected: A2 sustain now several times A5 sustain. If still equal, the setting
name is wrong or `bind_wiring` isn't being called — check with a temporary
`fprintf` on the push count, and remove it before committing.

- [ ] **Step 7: Gate**

Run: `python tools/null_gate_perform_source.py`
Expected: `196/196 identical`. No gated patch uses `dynamicPins` yet, so the
new pass must be a no-op for all of them.

- [ ] **Step 8: Commit**

```bash
git add engine/src/patch_loader.cpp engine/include/mforce/render/instrument.h patches/baselines/perform/wiring_setting.json
git commit -m "feat(engine): dynamicPins — settings driven once per note"
```

---

### Task 4: Bend-swap classification for wired pins

**Files:**
- Modify: `engine/src/patch_loader.cpp` (`bind_wiring` gains a second walk)

**Interfaces:**
- Consumes: `bind_wiring(…)` from Task 3.
- Produces: `vg.bendSwaps` entries for wiring-format patches, membership
  identical to `build_bindings`.

**Why:** P1 keeps the legacy `PitchBendSource` graft and populates
`vg.bendSwaps` for exactly the entries whose **freq** curve is empty — bare
targets and vcurve-only targets both (`patch_loader.cpp:822-825`). A converted
patch must land in the same set or the 4 bend/slide baselines diff in Task 6.
P3 deletes all of this; until then reproduce it faithfully.

The graph-shape rule that reproduces it exactly: **a wired fixed pin is a
bend-swap target when the path from that pin down to a
`PerformNode(field=frequency)` crosses no `CurveNode`.** A bare ref qualifies;
`Combined(PerformNode, CurveNode(velocity))` — the vcurve-only shape — also
qualifies, because the frequency leg is direct. A freq-curve chain does not.

- [ ] **Step 1: Add the JSON-space path walk**

Inside `bind_wiring`, above the per-node loop. It works on JSON, not built
objects, because the question is which node *types* lie on the path:

```cpp
    // True when `startId` reaches a frequency PerformNode along at least one
    // path crossing no CurveNode. Mirrors build_bindings' graft rule:
    // "empty freq curve", vcurve-only shapes included.
    std::unordered_map<std::string, int> memo;   // 0 = no, 1 = yes
    std::function<bool(const std::string&)> direct_freq =
        [&](const std::string& nid) -> bool {
            auto m = memo.find(nid);
            if (m != memo.end()) return m->second == 1;
            memo[nid] = 0;                       // cycle guard: assume no
            auto it = nodeMap.find(nid);
            if (it == nodeMap.end()) return false;
            const auto& n = it->second;
            const std::string t = n.at("type").get<std::string>();
            if (t == "CurveNode") return false;  // a curve on the path disqualifies
            if (t == "PerformNode") {
                const bool isFreq =
                    !n.contains("params") ||
                    n["params"].value("field", std::string("frequency")) == "frequency";
                memo[nid] = isFreq ? 1 : 0;
                return isFreq;
            }
            if (!n.contains("params")) return false;
            for (const auto& [k, v] : n["params"].items()) {
                if (v.is_object() && v.contains("ref") &&
                    direct_freq(v.at("ref").get<std::string>())) {
                    memo[nid] = 1;
                    return true;
                }
                if (v.is_array())
                    for (const auto& e : v)
                        if (e.is_object() && e.contains("ref") &&
                            direct_freq(e.at("ref").get<std::string>())) {
                            memo[nid] = 1;
                            return true;
                        }
            }
            return false;
        };
```

Add `#include <functional>` if not already present.

- [ ] **Step 2: Emit the bend swaps in the same node loop**

After the `dynamicPins` block, inside the per-node loop:

**Chain-internal pins must be excluded**, or the graft lands in the wrong
place. Legacy grafts exactly one pin per paramMap entry — the target pin on the
consumer — never a pin *inside* the transfer chain feeding it.
`CurveNode.source` is the only chain-internal input that is a
`param_descriptor`, so without this the converted curve chain grafts the
curve's own **x input**, and a bent note swaps what the curve reads instead of
leaving it alone. `CombinedSource` (the vcurve multiply) escapes anyway,
because `source1`/`source2` are `input_descriptors` and only
`param_descriptors` are scanned.

Caught by A/B, not by inspection: legacy renders a curve-fed bent note
identically with and without the bend (proving it does not graft), and the
wiring path did not match until this exclusion existed.

```cpp
        const std::string nodeType = node.at("type").get<std::string>();
        if (nodeType == "CurveNode" || nodeType == "PerformNode") continue;
        if (!node.contains("params")) continue;
        for (const auto& desc : consumer->param_descriptors()) {
            if (!node["params"].contains(desc.name)) continue;
            const auto& v = node["params"].at(desc.name);
            if (!v.is_object() || !v.contains("ref")) continue;
            const std::string srcId = v.at("ref").get<std::string>();
            if (!direct_freq(srcId)) continue;   // freq-curve chain: no graft
            auto srcIt = g.valueNodes.find(srcId);
            if (srcIt == g.valueNodes.end()) continue;
            // `restore` is what the pin holds on unbent notes — the chain
            // itself, exactly as build_bindings records it.
            vg.bendSwaps.push_back({consumer, desc.name, srcIt->second});
        }
```

The asymmetry is deliberate: `input_descriptors` pins (CombinedSource's
`source1`/`source2` and friends) are **not** scanned. The legacy graft only
touched `param_descriptors` pins backed by a ConstantSource; grafting a
combiner input would be new behavior, not a reproduction.

- [ ] **Step 3: Build and gate**

Expected: `196/196 identical`. Still no gated patch in wiring format, so this
must remain a no-op. The real proof is Task 6.

- [ ] **Step 4: Commit**

```bash
git add engine/src/patch_loader.cpp
git commit -m "feat(engine): bend-swap classification for wiring-format pins"
```

---

### Task 5: The paramMap → wiring converter

**Files:**
- Create: `tools/parammap_to_wiring.py`

**Interfaces:**
- Produces: `python tools/parammap_to_wiring.py <in.json> <out.json>` — rewrites
  one patch; exits 1 with a message on anything it cannot express.
- Produces: `python tools/parammap_to_wiring.py --check <in.json>` — exit 0 if
  the patch has no `paramMap`, 2 if it has one.
- Consumes: `mforce_cli --dump-descriptors` (`tools/mforce_cli/main.cpp:981`),
  which emits per-type `settings` straight from the registry after Task 1.
  Cached in `renders/scratch/descriptors.json`. **Do not hand-copy a settings
  list into this tool** — that is how the patch linter cried wolf three times,
  most recently on 59 false findings in run 25.

**Emission rules** (mirroring `build_bindings`' matrix in JSON space):

| paramMap entry | emitted wiring |
|---|---|
| bare `"node.pin"` | pin refs `__perf_freq` |
| `{target, curve}` | a `CurveNode` (`logx`, or `loglog` when `interp=="loglog"`) reading `__perf_freq`; pin refs it |
| `{target, vcurve}` | a `CombinedSource` multiply of the freq leg and a `linear` CurveNode reading `__perf_vel`; pin refs the multiply |
| `{target, curve, vcurve}` | both, multiply of the two chains |
| target with no `.` | pin name defaults to `"frequency"` (`patch_loader.cpp:797`) |
| name != `"frequency"` | dropped with a printed note — the old engine ignored these too (`patch_loader.cpp:783`) |

"pin refs X" means `nodes[target].params[pin] = {"ref": X}` when `pin` is a
fixed pin, and `nodes[target].dynamicPins[pin] = {"ref": X}` when it is a
**setting** — decided by the descriptor dump, never by name matching.

Synthesized nodes are **prepended** to `nodes` in dependency order
(`__perf_freq`, `__perf_vel`, curves, multiplies), because `build_graph`
resolves refs against nodes already built, in array order.

- [ ] **Step 1: Write the converter**

Create `tools/parammap_to_wiring.py`:

```python
"""Rewrite a patch's legacy instrument.paramMap as pure graph wiring
(plan_perform_source_p2a.md Task 5).

Mirrors the loader's build_bindings matrix (engine/src/patch_loader.cpp) in
JSON space. The proof that the mirror is faithful is the conversion null gate
(tools/null_gate_wiring.py): every converted patch must render bit-identical
to the frozen manifest.

  python tools/parammap_to_wiring.py in.json out.json
  python tools/parammap_to_wiring.py --check in.json      # 0 = nothing to do
"""
import json, subprocess, sys
from collections import OrderedDict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
DESC_CACHE = ROOT / "renders/scratch/descriptors.json"

PERF_F = "__perf_freq"
PERF_V = "__perf_vel"


def descriptors():
    """{typeName: {"params": [...], "settings": [...], ...}} straight from the
    registry. Regenerated whenever the exe is newer than the cache, so a
    rebuilt engine cannot leave a stale map behind."""
    if (DESC_CACHE.exists()
            and DESC_CACHE.stat().st_mtime > CLI.stat().st_mtime):
        return json.loads(DESC_CACHE.read_text(encoding="utf-8"))
    r = subprocess.run([str(CLI), "--dump-descriptors"],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        raise RuntimeError("mforce_cli --dump-descriptors failed: " + r.stderr)
    DESC_CACHE.parent.mkdir(parents=True, exist_ok=True)
    DESC_CACHE.write_text(r.stdout, encoding="utf-8")
    return json.loads(r.stdout)


def node_container(doc):
    """Patches come in two shapes: nodes at top level, or nested under a
    "graph" key ({"graph": {"nodes": [...], "output": ...}}) — Piano_bright.json
    is the latter. Returns the dict that OWNS the "nodes" list, so callers can
    both read and replace it."""
    g = doc.get("graph")
    if isinstance(g, dict) and isinstance(g.get("nodes"), list):
        return g
    if isinstance(doc.get("nodes"), list):
        return doc
    raise ValueError("patch has no node list at doc['nodes'] or doc['graph']['nodes']")


def convert(doc):
    """Returns (doc, n_entries_converted). Raises ValueError if inexpressible."""
    inst = doc.get("instrument")
    if not inst or "paramMap" not in inst:
        return doc, 0
    pm = inst["paramMap"]

    container = node_container(doc)
    by_id = {n["id"]: n for n in container["nodes"]}
    new_nodes = OrderedDict()
    counter = [0]

    def need_perf(field):
        nid = PERF_F if field == "frequency" else PERF_V
        if nid not in new_nodes:
            new_nodes[nid] = {"id": nid, "type": "PerformNode",
                              "params": {"field": field}}
        return nid

    def add_curve(knots, interp, src_id):
        counter[0] += 1
        nid = "__curve_%d" % counter[0]
        new_nodes[nid] = {"id": nid, "type": "CurveNode",
                          "params": {"interp": interp,
                                     "knots": [[float(a), float(b)] for a, b in knots],
                                     "source": {"ref": src_id}}}
        return nid

    def add_mul(a_id, b_id):
        counter[0] += 1
        nid = "__mul_%d" % counter[0]
        new_nodes[nid] = {"id": nid, "type": "CombinedSource",
                          "params": {"source1": {"ref": a_id},
                                     "source2": {"ref": b_id},
                                     "operation": "multiply", "gainAdj": 0.0}}
        return nid

    def chain_for(entry):
        if isinstance(entry, str):
            return entry, need_perf("frequency")
        target = entry["target"]
        head = need_perf("frequency")
        if "curve" in entry:
            interp = "loglog" if entry.get("interp") == "loglog" else "logx"
            head = add_curve(entry["curve"], interp, head)
        if "vcurve" in entry:
            v = add_curve(entry["vcurve"], "linear", need_perf("velocity"))
            head = add_mul(head, v)
        return target, head

    desc = descriptors()
    converted = 0
    for name, entries in pm.items():
        if name != "frequency":
            print("  note: paramMap name %r dropped (the engine never consumed "
                  "it either)" % name, file=sys.stderr)
            continue
        for entry in (entries if isinstance(entries, list) else [entries]):
            target, head = chain_for(entry)
            node_id, _, pin = target.partition(".")
            pin = pin or "frequency"
            if node_id not in by_id:
                raise ValueError("paramMap target %r: no node %r" % (target, node_id))
            node = by_id[node_id]
            ntype = node["type"]
            if ntype not in desc:
                raise ValueError("node %r has unregistered type %r — cannot tell "
                                 "a setting from a fixed pin" % (node_id, ntype))
            # Settings land in dynamicPins; fixed pins in params.
            # The registry decides, not a name list.
            slot = "dynamicPins" if pin in desc[ntype]["settings"] else "params"
            node.setdefault(slot, {})[pin] = {"ref": head}
            converted += 1

    del inst["paramMap"]
    container["nodes"] = list(new_nodes.values()) + container["nodes"]
    return doc, converted


def main():
    if "--check" in sys.argv:
        doc = json.load(open(sys.argv[-1], encoding="utf-8"))
        sys.exit(2 if "paramMap" in doc.get("instrument", {}) else 0)
    src, dst = sys.argv[1], sys.argv[2]
    doc = json.load(open(src, encoding="utf-8"), object_pairs_hook=OrderedDict)
    doc, n = convert(doc)
    with open(dst, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)
    print("converted %d entr%s -> %s" % (n, "y" if n == 1 else "ies", dst))


main()
```

**Note on patch shape:** the two patch shapes (nodes at top level vs nested
under `graph`) are handled by `node_container()` above. `Piano_bright.json` is
a nested one and is used deliberately as a test case in Step 4 — if that
renders identical, both shapes work.

- [ ] **Step 2: Convert one bare-target patch and read the output**

```
python -c "import json,glob; [print(f) for f in sorted(glob.glob('patches/library/**/*.json',recursive=True)) if isinstance(json.load(open(f,encoding='utf-8')).get('instrument',{}).get('paramMap',{}).get('frequency'), str)][:3]"
```
Convert the first to `renders/scratch/conv1.json` and read it. First node must
be `__perf_freq`; the target node's frequency pin must be `{"ref":
"__perf_freq"}`; `instrument.paramMap` must be gone.

- [ ] **Step 3: Render both, compare hashes**

```
./build/tools/mforce_cli/Release/mforce_cli.exe <the original patch> renders/scratch/a.wav
./build/tools/mforce_cli/Release/mforce_cli.exe renders/scratch/conv1.json renders/scratch/b.wav
python -c "import hashlib; h=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest(); a=h('renders/scratch/a.wav'); b=h('renders/scratch/b.wav'); print(a); print(b); print('IDENTICAL' if a==b else 'DIFF')"
```
Expected: `IDENTICAL`. If not, stop — one patch is far cheaper to diagnose
than 118.

- [ ] **Step 4: Repeat for one curve patch, one vcurve patch, one setting patch**

Find them:
```
python -c "
import json,glob
for f in sorted(glob.glob('patches/library/**/*.json',recursive=True)+glob.glob('patches/baselines/**/*.json',recursive=True)):
    d=json.load(open(f,encoding='utf-8'))
    pm=d.get('instrument',{}).get('paramMap',{})
    for n,e in pm.items():
        for x in (e if isinstance(e,list) else [e]):
            if isinstance(x,dict) and 'vcurve' in x: print('VCURVE',f)
            elif isinstance(x,dict) and 'curve' in x: print('CURVE',f)
" | sort -u | head
```
For the setting case use `patches/library/keys/Piano_bright.json` (five
settings on `KSPianoString1`, and the nested-`graph` shape). Convert, render,
compare. Expected: `IDENTICAL` each time.

- [ ] **Step 5: Commit**

```bash
git add tools/parammap_to_wiring.py
git commit -m "feat(tools): paramMap -> wiring format converter"
```

---

### Task 6: The conversion null gate

**Files:**
- Create: `tools/null_gate_wiring.py`

**Interfaces:**
- Consumes: `tools/parammap_to_wiring.py`, `tools/null_gate_manifest.json`.
- Produces: `python tools/null_gate_wiring.py` — exit 0 = every converted patch
  renders identically to its pre-PerformSource hash.

This is the task that proves P2a. Everything before it is machinery.

- [ ] **Step 1: Write the gate**

Create `tools/null_gate_wiring.py`:

```python
"""Conversion null gate (plan_perform_source_p2a.md Task 6).

Every patch in the P1 manifest is rewritten into wiring format by
tools/parammap_to_wiring.py, rendered, and compared against the hash the SAME
patch produced before PerformSource existed. A converted patch that renders one
byte differently means the wiring format does not say what the paramMap said.

Patches with no paramMap are converted trivially (no-op) and still rendered — a
no-op conversion that changes the audio would mean the tool is corrupting files
it claims not to touch.
"""
import hashlib, json, subprocess, sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
MANIFEST = ROOT / "tools/null_gate_manifest.json"
TMP_JSON = ROOT / "renders/scratch/wiring_gate.json"
TMP_WAV = ROOT / "renders/scratch/wiring_gate.wav"


def convert_file(src: Path) -> int:
    r = subprocess.run([sys.executable, str(ROOT / "tools/parammap_to_wiring.py"),
                        str(src), str(TMP_JSON)],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0:
        print("CONVERT_FAIL", src, r.stderr.strip(), flush=True)
    return r.returncode


def render_hash(patch: Path) -> str:
    if TMP_WAV.exists():
        TMP_WAV.unlink()
    r = subprocess.run([str(CLI), str(patch), str(TMP_WAV)],
                       capture_output=True, text=True, cwd=ROOT)
    if r.returncode != 0 or not TMP_WAV.exists():
        return f"RENDER_FAIL:{r.returncode}"
    return hashlib.sha256(TMP_WAV.read_bytes()).hexdigest()


def main():
    ref = json.loads(MANIFEST.read_text())
    bad = converted = 0
    for key, want in sorted(ref.items()):
        src = ROOT / key
        had_pm = "paramMap" in json.loads(
            src.read_text(encoding="utf-8")).get("instrument", {})
        if convert_file(src) != 0:
            bad += 1
            continue
        converted += 1 if had_pm else 0
        got = render_hash(TMP_JSON)
        if got != want:
            bad += 1
            print(f"DIFF {key} (paramMap={had_pm}): {want} -> {got}", flush=True)
    print(f"{len(ref) - bad}/{len(ref)} identical after conversion "
          f"({converted} carried a paramMap)")
    sys.exit(1 if bad else 0)


main()
```

- [ ] **Step 2: Run it**

Run: `python tools/null_gate_wiring.py`
Expected: `196/196 identical after conversion (118 carried a paramMap)`, exit 0.

- [ ] **Step 3: If there are diffs, triage by class before fixing anything**

The three risk classes have different fixes; a shotgun edit masks which fired:

```
python tools/null_gate_wiring.py 2>&1 | python -c "
import sys, json
for line in sys.stdin:
    if not line.startswith('DIFF'): continue
    key = line.split()[1]
    d = json.load(open(key, encoding='utf-8'))
    g = d.get('graph'); nl = g.get('nodes') if isinstance(g, dict) else d.get('nodes')
    types = {n['type'] for n in nl}
    pm = d.get('instrument', {}).get('paramMap', {})
    flat = [x for e in pm.values() for x in (e if isinstance(e, list) else [e])]
    print(('MULTIPLEX' if 'MultiplexSource' in types else
           'VCURVE'    if any(isinstance(x, dict) and 'vcurve' in x for x in flat) else
           'CURVE'     if any(isinstance(x, dict) and 'curve' in x for x in flat) else
           'BARE'), key)
" | sort | uniq -c
```

- `MULTIPLEX` → risk 2. Check whether `extract_subgraph_json` pulls the chain
  into the subtree at all, and whether it follows `dynamicPins` refs.
- `BARE`/`CURVE` on a bend/slide baseline → risk 3, Task 4's classifier.
  Compare `vg.bendSwaps.size()` between the two load paths with a temporary
  `fprintf`.
- Broad diffs across unrelated patches → risk 1, RefSource wrapping. Apply the
  stated fallback.

- [ ] **Step 4: Run both gates**

`python tools/null_gate_perform_source.py` then `python tools/null_gate_wiring.py`.
Expected: `196/196` from each. The first proves the legacy path still works,
the second proves the new format says the same thing.

- [ ] **Step 5: Commit**

```bash
git add tools/null_gate_wiring.py
git commit -m "test: conversion null gate — 196/196 identical from wiring format"
```

---

### Task 7: Record what landed

**Files:**
- Modify: `docs/perform_source_design.md` (§7)
- Modify: `docs/pin_model_design.md` (§11 — note the JSON key that shipped)
- Modify: `docs/autonomy/dsp/BACKLOG.md` (item 20)

- [ ] **Step 1: Update the phase list**

In `docs/perform_source_design.md` §7, mark P2a landed with the conversion-gate
result and the `setting` rename, and leave P2b as the remaining half.

- [ ] **Step 2: Close the loop in the pin-model spec**

In `docs/pin_model_design.md` §11, record that the serialization shipped as a
per-node `dynamicPins` object, so the spec names the key that actually exists.

- [ ] **Step 3: Update BACKLOG item 20**

Rewrite it to say P2a landed — rename, PerformNode, dynamicPins, converter,
196/196 conversion gate — and that P2b (editor nodes, knot editor, grey→gold
promotion, derived views, UI serializer) is what remains. Keep the standing
warning that UI re-save drops `Envelope.minValue/maxValue/nominal` until the
serializer learns them.

- [ ] **Step 4: Commit**

```bash
git add docs/perform_source_design.md docs/pin_model_design.md docs/autonomy/dsp/BACKLOG.md
git commit -m "docs: PerformSource P2a landed — wiring format, 196/196 conversion gate"
```
