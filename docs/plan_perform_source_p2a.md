# PerformSource P2a Implementation Plan — the wiring format

> ## ⛔ PARKED 2026-08-19 — DO NOT EXECUTE
>
> Parked by Matt during review. The plan decides *where in the file* a chain
> landing on a config gets written (the `bindings` object) without the model
> question having been settled first: what a config pin **is**, how it reads
> on a node, and how a user attaches one. Spec §5 covers it in one sentence
> ("landing on a config pin — one visual language, two evaluation times,
> distinguished by pin type") and no more.
>
> The file format should follow that model, not lead it. Superseded by the
> brainstorm on config chains; revise or rewrite this plan afterwards.
>
> Also carried into the brainstorm: the `bindings`-vs-`params` cost estimate
> in here is wrong. It claims ~40 configurators would need teaching; the load
> path builds registry instances *before* handing over params, so descriptors
> are already known at that point and only the hand-rolled construction
> branches read blind — of which only `Envelope` reads a config key. Re-cost
> before deciding.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

---

## Read this part first (plain language)

### What's true today

A patch file has a block called `paramMap`. It's the list of "when a note
plays, send its pitch to *this* setting on *that* node" instructions —
sometimes straight through, sometimes bent through a curve first.

P1 (landed yesterday) changed what happens to that block when a patch loads.
It used to be handled by special-case machinery bolted onto the side of the
voice. Now it gets turned into ordinary graph parts — a node that reports the
note's pitch, a curve node, a wire — the same kinds of parts the rest of the
patch is made of. Nothing sounds different; all 196 patches render byte for
byte identical.

### What's missing

Those parts only exist in memory. Nothing can write them back out to a file.
There is no way to *spell* "a node that reports the note's pitch" in patch
JSON — the loader invents it during load and it dies when the patch unloads.

That matters because the next step (P2b) teaches the UI's Save to write this
stuff. Save has to write *something*, and right now there's no spelling for it.

### What P2a does

Defines the spelling, and proves it says the same thing the old one did.

1. A new node type you can write in a patch file — `PerformNode` — that
   reports the note's pitch or its velocity. Put as many as you like in a
   patch; they all report the same note.
2. Teaches the loader to accept a wire pointing at a *config* setting (see the
   decision below).
3. A converter that rewrites any patch's `paramMap` into the new spelling.
4. The proof: take all 196 patches, run them through the converter, render
   them, and require every single one to come out **byte for byte identical**
   to what it renders today. Not "sounds the same" — identical.

### What it costs you

Nothing changes in how patches load or sound. `paramMap` keeps working and
keeps being read; this adds a second way to say the same thing, it doesn't
retire the first. Your patch files are not touched — the converter writes to a
scratch copy. Files only change format when the UI saves them, which is P2b.

Two new baseline patches land in `patches/baselines/perform/`, written in the
new format as smoke tests.

### Decisions in here you may want to veto

**1. Where "this curve's output goes to that config" gets written down.**

Node settings come in two kinds. **Pins** can be fed from another node — a
wire into a pin is normal and already works. **Configs** are plain numbers,
read once when the node is built and updated only by being handed a new
number: envelope sustain level, the KS string's t60/brightness/dispersion.

Nothing is ever *wired* to a config, and P2a does not change that. A `paramMap`
entry aimed at a config is evaluated once at note-on and the resulting number
is handed over via `set_config` — that timing is identical before and after.
Four patches do this, all in the viola family, all through a curve:

```
viola_default.json:   {"target": "env2.sustainLevel",
                       "curve": [[50, 0.08], [400, 0.4], [1200, 20.0]]}
```

(`Envelope::set_config` has a deliberate special case for this,
`envelope.h:196`, unclamped — which is why 20.0 there is legal.)

The problem is bookkeeping, not signal. Today the whole instruction lives
*outside* the node, in the `paramMap` block, so the envelope's JSON never
contains anything unusual. The new spelling turns the curve into real nodes,
and then something has to record where that curve's output lands —
`env2.sustainLevel` — inside the envelope node's own JSON. That is exactly the
slot the envelope's construction code reads expecting a number. Put the record
there and the patch fails to load.

- **Chosen:** the record goes in its own small section of the node, called
  `bindings`, where the node-building code never looks. Nothing else changes.
- **Rejected:** put it with everything else in `params`. Reads nicer, but ~40
  node types would each need teaching to tolerate it, and the failure mode is
  a patch that won't load at all.

No audible difference either way. It's about what the file looks like and how
much of the loader has to move.

**2. A converter exists at all.** It wasn't in the spec — added here as the
only way to get 196 patches into the new format for testing before the UI can
write it. It's also what P4's bulk migration will use.

**3. `PerformNode` is one node per thing reported** — one for pitch, one for
velocity — rather than a single node with several outputs. Matches how the
engine is already built; the alternative would change how every node type in
the engine resolves its wires, to serve this one case.

### How you'll know it worked

`python tools/null_gate_wiring.py` prints `196/196 identical after
conversion`. If it prints anything else, the format doesn't say what
`paramMap` said, and the plan stops there rather than papering over it.

---

## The rest is execution detail

**Goal:** Give the bindings P1 builds in memory a **serialized form** — a
`PerformNode` JSON node type, wireable config pins, and a converter that
rewrites any legacy `paramMap` into pure graph wiring — proven by rendering
all 196 gated patches from their *converted* files and matching the frozen
manifest bit for bit.

**Architecture:** P1's `build_bindings()` converts `paramMap` → CurveNode
chains + wires + PushBindings at load, but nothing can write those back out:
`freqOut`/`velOut` are constructed by the loader and no JSON node resolves to
them. P2a closes that. The voice's `PerformSource` and its two `PerformOut`
adapters are created **before** `build_graph`, handed down as a context, and a
`{"type":"PerformNode","params":{"field":"frequency"}}` node resolves to the
shared adapter — one JSON node per field, because each `PerformOut` already is
a separate one-output `ValueSource`. Config keys learn to accept `{"ref": …}`
and become Setup-evaluated PushBindings. The legacy `paramMap` path stays
untouched and is read forever; the two formats coexist.

**Not in P2a:** all UI work — PerformNode/CurveNode as editor nodes, the knot
editor, Curves/Mappings as derived views, the UI serializer. That is P2b. The
split is about proof order, not about scheduling around anything: the UI's
Save is what will start writing this format into Matt's patch files, so the
format gets nailed down and gated against all 196 patches *before* the editor
is taught to emit it.

**Tech Stack:** C++20 (MSVC), CMake (VS-bundled:
`C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe`),
nlohmann::json, Python 3.

## Global Constraints

- **The null gate is the law.** `python tools/null_gate_perform_source.py`
  must stay 196/196 after every task. A diff = stop and diagnose, never
  rationalize.
- **A running `mforce_ui.exe` is not a reason to change the plan.** If the exe
  is locked, rename it out of the way and link the new one (run-19 precedent —
  `mforce_ui_running_backup2.exe` in the build dir is that trick's residue);
  the running process keeps going off the renamed file and picks up the new
  build on its next restart. If the rename fails, say so and ask Matt to close
  it — do not silently reshape the work around it.
- No heap allocation in `next()` paths (CLAUDE.md non-negotiable).
- Verify the current branch live (`git branch --show-current`) before each
  commit — two Claudes share this working copy.
- Stage tracked changes with `git add -u` plus explicit paths for new files;
  never `git add <dir>/`.
- Commit trailer: `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`
- New JSON keys are optional with defaults that reproduce current behavior.
- `__`-prefixed node ids are reserved for synthesized nodes (the UI already
  reserves the prefix, `tools/mforce_ui/main.cpp:1740`).
- Legacy `paramMap` keeps working unchanged. P2a **adds** a format; it removes
  nothing.

## Known bit-identity risks (watch these; the gate decides)

1. **RefSource wrapping.** `build_bindings` calls `set_param` directly, so a
   chain feeding N pins is shared raw. The generic param pass goes through
   `resolve_param`, which wraps 2nd+ uses in `RefSource`
   (`engine/src/patch_loader.cpp:286-295`). `PerformOut` and `CurveNode` are
   both documented idempotent, so wrapping should be value-identical.
   **If Task 5 shows diffs on multi-consumer patches**, the fix is to exempt
   `PerformOut`/`CurveNode` from usage counting in `resolve_param`, not to
   change the converter.
2. **Multiplex clones.** P1 pushes into clones (`set_clone_param`); wiring
   format instead pulls, because `extract_subgraph_json` drags the referenced
   chain into each clone's subtree and each clone builds its own CurveNode
   against the shared adapter. Values are equal; the 7 Multiplex patches are
   the proof. They are called out explicitly in Task 5. **Two things to check
   there before assuming the pull works:** `extract_subgraph_json` follows
   refs in `params` — it must learn to follow `bindings` refs too, or a config
   target inside a clone subtree loses its chain; and `bind_wiring` walks only
   top-level `nodeMap`, so a config binding *inside* a clone subtree is not
   seen at all. If either bites, the fallback is to keep those targets on the
   push path by matching P1: detect `vg.topMultiplex` in `bind_wiring` and
   emit a `PushBinding` with `targetNodeId` set, exactly as `build_bindings`
   case 2 does.
3. **Bend graft.** P1 keeps the legacy `PitchBendSource` graft and populates
   `vg.bendSwaps` for exactly the entries with an empty freq-curve. Wiring
   format must reproduce that membership by graph shape (Task 3) or the 4
   bend/slide baselines will diff.

## Format decision: config refs live in a per-node `bindings` object

Param refs go where they always have — in `params`, resolved by the generic
pass. **Config refs go in a sibling object**, `"bindings"`:

```json
{ "id": "env", "type": "Envelope",
  "params":   { "preset": "adsr", "sustainLevel": 0.7 },
  "bindings": { "sustainLevel": { "ref": "__curve_sus" } } }
```

The alternative — putting config refs in `params` alongside everything else —
was rejected because it detonates on contact. Node **construction** reads
`params` directly, before any descriptor is known: `envelope_from_preset_json`
does `p.value("sustainLevel", 0.7f)`, and nlohmann's `value()` throws
`type_error.302` when the stored value is an object rather than a number. Four
of the gated patches map frequency onto `sustainLevel`, so they would fail to
load at all — and the same pattern (`p.value(key, default)` at construction)
appears in most of the ~40 registered configurators, including the KS piano's
`t60`/`brightness`/`dispersion`/`inharmGain`. A blanket "filter object values
out of params before construction" pre-pass is not available either:
`MultiplexSource` legitimately reads a ref out of `params` at construction to
extract its subtree.

`bindings` costs one new key, cannot collide with any existing reader, and
says what it means — these are pushed once at Setup, not pulled. Spec §5's
"one visual language" is about the editor, which draws both kinds as wires and
distinguishes them by pin type; it does not require one JSON key. If the
`params`-only shape is wanted later, it is a converter change plus a
construction-time filter, not a format re-think.

---

### Task 0: Confirm the baseline is green

**Files:** none (verification only).

**Interfaces:**
- Produces: a known-good starting point. Every later task compares against it.

- [ ] **Step 1: Check whether the UI is holding the exe**

Run: `Get-Process mforce_ui -ErrorAction SilentlyContinue`

If it prints a process, rename the locked exe so links still succeed:

```
Rename-Item build/tools/mforce_ui/Release/mforce_ui.exe mforce_ui_locked.exe
```

If the rename fails, stop and ask Matt to close the UI. Either way, note in
the run report that the running UI is one build behind until he restarts it.

- [ ] **Step 2: Confirm the tree is on main and note what is dirty**

Run: `git branch --show-current` then `git status --short`
Expected: `main`. Two pre-existing dirty files are expected and are NOT ours
to commit: `engine/include/mforce/render/patch_loader.h` (minPolyphony) and
`tools/mforce_ui/CMakeLists.txt` (rtmidi). Leave them alone.

- [ ] **Step 3: Build the CLI**

Run:
```
& "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_cli
```
Expected: build succeeds.

- [ ] **Step 4: Run the gate**

Run: `python tools/null_gate_perform_source.py`
Expected: last line `196/196 identical`, exit 0.

If it is not 196/196, STOP. Something landed dirty; diagnose before writing a
line of P2a.

---

### Task 1: `PerformNode` JSON type

**Files:**
- Modify: `engine/src/patch_loader.cpp` (build_graph signature + dispatch +
  both instrument call sites + subgraph rebuild)
- Modify: `tools/engine_tests/main.cpp` (add `run_perform_node_tests`)
- Create: `patches/baselines/perform/wiring_smoke.json`

**Interfaces:**
- Produces: `struct PerformContext { std::shared_ptr<ValueSource> freqOut, velOut; };`
  in `engine/src/patch_loader.cpp` (file-local).
- Produces: `build_graph(nodeMap, nodeOrder, sampleRate, const PerformContext* perf)`
  — fourth parameter defaults to `nullptr`. Tasks 2 and 3 consume the same
  call sites.
- Produces: JSON node type `PerformNode` with one param, `field`
  (`"frequency"` | `"velocity"`, default `"frequency"`). Task 4's converter
  emits exactly this.

- [ ] **Step 1: Add the context struct and thread it through build_graph**

In `engine/src/patch_loader.cpp`, immediately above the forward declarations
at line 257 (`// Forward declarations for subgraph extraction …`), add:

```cpp
// Per-voice performance adapters, handed down so PerformNode instances all
// resolve to the SAME objects (perform_source_design.md §2.3: "All instances
// render the same outputs"). Null in NodeGraph/standalone contexts, where a
// PerformNode is an authoring error rather than a silent constant.
struct PerformContext {
    std::shared_ptr<ValueSource> freqOut, velOut;
};
```

Extend the forward declaration of `build_subgraph_with_seed_perturbation`
(line 266) and `build_graph` (line 271) to carry it:

```cpp
build_subgraph_with_seed_perturbation(
    const std::string& subtreeJsonStr,
    uint32_t seedPerturbation,
    int sampleRate,
    const PerformContext* perf);
```

```cpp
static GraphResult build_graph(
    const std::unordered_map<std::string, json>& nodeMap,
    const std::vector<std::string>& nodeOrder,
    int sampleRate,
    const PerformContext* perf = nullptr)
```

Apply the same two extra parameters to the *definition* of
`build_subgraph_with_seed_perturbation` (near line 696), and forward `perf` to
the `build_graph` call inside it.

Inside `build_graph`, the Multiplex branch's closure (line ~604) calls
`build_subgraph_with_seed_perturbation`. Capture `perf` by value in that
closure (it is a raw pointer into the caller's stack frame; the closure is
invoked during the same `build_graph` call, so this is safe — add the comment
saying so) and pass it through.

- [ ] **Step 2: Add the dispatch branch**

In `build_graph`'s type chain, directly after the `CurveNode` branch (which
ends at line 388), insert:

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

`attach_perform_source` (line 884) currently creates the adapters *after* the
graph. Reduce it to the conversion step only:

```cpp
// Convert the legacy paramMap, if the patch carries one. The performance
// objects themselves are now created BEFORE build_graph (PerformNode needs
// them during graph construction) — see the two voice loops.
static void attach_perform_source(const json& instJson, const GraphResult& g,
                                  PitchedInstrument::VoiceGraph& vg)
{
    if (instJson.contains("paramMap"))
        build_bindings(instJson["paramMap"], g, vg);
}
```

In **both** voice loops — `load_patch_file`'s instrument path (the `for (int v
= 0; v < polyphony; ++v)` at line 946) and `load_instrument_patch`'s (line
1141) — hoist the construction. The `load_instrument_patch` loop becomes:

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

Make the same edit at the line-946 loop, preserving whatever else that loop
already does between `build_graph` and `voicePool.push_back`.

- [ ] **Step 4: Build**

Run:
```
& "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target mforce_cli
```
Expected: succeeds.

- [ ] **Step 5: Run the gate — nothing may have moved yet**

Run: `python tools/null_gate_perform_source.py`
Expected: `196/196 identical`, exit 0. This step is the whole point of doing
the hoist as its own change: no patch uses `PerformNode` yet, so any diff here
is a pure refactor regression.

- [ ] **Step 6: Write the engine test**

In `tools/engine_tests/main.cpp`, add after `run_curve_node_tests`:

```cpp
#include "mforce/render/perform_source.h"

static void run_perform_node_tests() {
    // Two adapters over one store; both track set_note, both idempotent.
    auto ps = std::make_shared<PerformSource>();
    auto f = std::make_shared<PerformOut>(ps, PerformOut::Field::Frequency);
    auto v = std::make_shared<PerformOut>(ps, PerformOut::Field::Velocity);

    ps->set_note(220.0f, 0.25f, 4800);
    f->next(); v->next();
    CHECK_NEAR(f->current(), 220.0f, 1e-6f);
    CHECK_NEAR(v->current(), 0.25f,  1e-6f);

    // Idempotent: repeated next() between set_note calls does not drift.
    for (int i = 0; i < 8; ++i) f->next();
    CHECK_NEAR(f->current(), 220.0f, 1e-6f);

    // A CurveNode reading the shared adapter re-evaluates on the next note.
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

Call it from `main` alongside the existing suites.

- [ ] **Step 7: Run the engine tests**

Run:
```
& "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release --target engine_tests
```
then `./build/tools/engine_tests/Release/engine_tests.exe`
Expected: `0 failures`.

- [ ] **Step 8: Write the hand-authored smoke patch**

Create `patches/baselines/perform/wiring_smoke.json` — the first patch written
in wiring format, deliberately exercising a PerformNode straight into a pin
and a PerformNode through a CurveNode:

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

Names verified against the registry while writing this plan: `SineSource` is
registered (`source_registrations.cpp:56`) and inherits `frequency` /
`amplitude` / `phase` from `WaveSource` (`dsp_wave_source.h:75-81`); the adsr
preset's sustain key is `sustainLevel`, not `sustain`
(`envelope_json.h:41`) — a wrong key here is silently defaulted, not
reported.

- [ ] **Step 9: Render it and confirm both mechanisms fired**

Run: `./build/tools/mforce_cli/Release/mforce_cli.exe patches/baselines/perform/wiring_smoke.json renders/scratch/wiring_smoke.wav`
Expected: exit 0, a WAV written, no `[loader]` warnings.

Then confirm the curve is *doing something* — the A5 note must be quieter than
the A2 note, because `__curve_bright` maps 110→0.9 and 880→0.2:

```
python -c "import wave,struct,sys; w=wave.open('renders/scratch/wiring_smoke.wav'); n=w.getnframes(); sr=w.getframerate(); ch=w.getnchannels(); d=struct.unpack('<%dh'%(n*ch), w.readframes(n)); seg=lambda a,b: max(abs(x) for x in d[int(a*sr)*ch:int(b*sr)*ch]); lo=seg(0.0,0.8); hi=seg(1.0,1.8); print('A2 peak',lo,'A5 peak',hi,'ratio',hi/lo); sys.exit(0 if hi < lo*0.6 else 1)"
```
Expected: prints a ratio well under 0.6 and exits 0. If the peaks are equal,
the CurveNode is not wired — diagnose before continuing.

- [ ] **Step 10: Confirm the guard fires in NodeGraph mode**

Make a throwaway copy with the `instrument` block removed:

```
python -c "import json; d=json.load(open('patches/baselines/perform/wiring_smoke.json')); d.pop('instrument'); json.dump(d, open('renders/scratch/no_inst.json','w'), indent=1)"
```
Run: `./build/tools/mforce_cli/Release/mforce_cli.exe renders/scratch/no_inst.json renders/scratch/x.wav`
Expected: non-zero exit with the message `PerformNode '__perf_freq': no voice
context`. A silent success here means the guard is not on the NodeGraph path
— find the other `build_graph` call site and check why `perf` is non-null.

- [ ] **Step 11: Commit**

```bash
git add -u && git add tools/engine_tests/main.cpp patches/baselines/perform/wiring_smoke.json
git commit -m "feat(engine): PerformNode JSON type — voice adapters as graph citizens"
```

---

### Task 2: Config pins accept refs

**Files:**
- Modify: `engine/src/patch_loader.cpp` (new `bind_wiring` pass + call sites)
- Create: `patches/baselines/perform/wiring_config.json`

**Interfaces:**
- Consumes: `PerformContext`, `build_graph(…, perf)` from Task 1.
- Produces: `static void bind_wiring(const std::unordered_map<std::string, json>& nodeMap,
  const std::vector<std::string>& nodeOrder, const GraphResult& g,
  PitchedInstrument::VoiceGraph& vg);` — Task 3 extends this same function.

**Why:** nothing reads a node's `bindings` object today, so a converted patch
would silently lose every config target. 118 of the 196 gated patches carry a
paramMap and their targets include real configs — `sustainLevel` ×4
(`envelope.h:176`) and the KS piano's `t60` / `brightness` / `dispersion` /
`inharmGain`. Without this the converter cannot express them.

- [ ] **Step 1: Write the failing patch**

Create `patches/baselines/perform/wiring_config.json`. It maps note frequency
onto an Envelope's `sustainLevel` **config** through a curve — low notes
sustain, high notes do not. Note `sustainLevel` appears twice and that is
correct: the scalar in `params` is what construction reads, the ref in
`bindings` is what overrides it per note.

```json
{
  "sampleRate": 48000,
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
      "bindings": { "sustainLevel": { "ref": "__curve_sus" } } },
    { "id": "out", "type": "CombinedSource",
      "params": { "source1": { "ref": "tone" }, "source2": { "ref": "env" },
                  "operation": "multiply", "gainAdj": 0.0 } }
  ],
  "output": "out",
  "instrument": { "polyphony": 4, "volume": 1.0 },
  "score": [ { "note": 57, "start": 0.0, "duration": 0.8, "velocity": 0.8 },
             { "note": 81, "start": 1.0, "duration": 0.8, "velocity": 0.8 } ],
  "seconds": 2.0
}
```

- [ ] **Step 2: Render it and watch it do nothing**

Run: `./build/tools/mforce_cli/Release/mforce_cli.exe patches/baselines/perform/wiring_config.json renders/scratch/wiring_config.wav`

Then measure the sustain plateau of each note:

```
python -c "import wave,struct; w=wave.open('renders/scratch/wiring_config.wav'); n=w.getnframes(); sr=w.getframerate(); ch=w.getnchannels(); d=struct.unpack('<%dh'%(n*ch), w.readframes(n)); seg=lambda a,b: max(abs(x) for x in d[int(a*sr)*ch:int(b*sr)*ch]); print('A2 sustain',seg(0.4,0.7),'A5 sustain',seg(1.4,1.7))"
```
Expected NOW: the two numbers are roughly equal — the `bindings` object was
ignored and both notes used the patch's own `sustainLevel` 0.7. This is the
failing test.

- [ ] **Step 3: Add the bind_wiring pass**

In `engine/src/patch_loader.cpp`, directly after `build_bindings` ends (line
880), add:

```cpp
// ---------------------------------------------------------------------------
// Wiring-format bindings (perform_source_design.md §5, P2a). The generic
// param pass already wires refs on ordinary pins; this pass handles what it
// structurally cannot: a node's "bindings" object, which names CONFIG keys.
// Configs rebuild state in prepare and have no pointer to pull, so the chain
// is evaluated ONCE per note and pushed via set_config. They live outside
// "params" because construction reads params directly, before any descriptor
// is known, and nlohmann's value() throws on an object where it wants a
// number (see the format-decision section of plan_perform_source_p2a.md).
// Task 3 adds bend-swap classification to this same walk.
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

        if (node.contains("bindings")) {
            for (const auto& [key, v] : node["bindings"].items()) {
                if (!v.is_object() || !v.contains("ref"))
                    throw std::runtime_error("wiring: '" + id + ".bindings." + key +
                                             "' must be a {\"ref\": …} object");
                bool isConfig = false;
                for (const auto& desc : consumer->config_descriptors())
                    if (key == desc.name) { isConfig = true; break; }
                if (!isConfig)
                    throw std::runtime_error("wiring: '" + id + ".bindings." + key +
                        "' is not a config on " + consumer->type_name() +
                        " (ordinary pins belong in \"params\")");

                const std::string srcId = v.at("ref").get<std::string>();
                auto srcIt = g.valueNodes.find(srcId);
                if (srcIt == g.valueNodes.end())
                    throw std::runtime_error("wiring: config '" + id + "." + key +
                                             "' refs unknown node '" + srcId + "'");

                PitchedInstrument::PushBinding b;
                b.consumer     = consumer;
                b.paramName    = key;
                b.targetNodeId = id;
                b.chain        = srcIt->second;
                b.cs           = nullptr;
                b.isConfig     = true;
                vg.pushBindings.push_back(std::move(b));
            }
        }
    }
}
```

- [ ] **Step 4: Call it from both voice loops**

In both loops edited in Task 1, add the call right after
`attach_perform_source(instJson, g, vg);`:

```cpp
        bind_wiring(nodeMap, nodeOrder, g, vg);
```

Order matters and is deliberate: `build_bindings` (legacy) runs first so a
patch carrying both formats resolves the legacy entries exactly as it does
today, and the wiring pass appends. No gated patch carries both; the ordering
is documented so that a hybrid, if one ever appears, is deterministic.

- [ ] **Step 5: Rebuild and re-measure**

Run the build command from Task 1 Step 4, then re-render and re-measure with
the same python one-liner from Step 2.
Expected: A2 sustain is now several times A5 sustain. If they are still equal,
the config name is wrong or `bind_wiring` is not being called — check with a
temporary `fprintf` on the push count, and remove it before committing.

- [ ] **Step 6: Run the gate**

Run: `python tools/null_gate_perform_source.py`
Expected: `196/196 identical`. No gated patch uses wiring format yet, so the
new pass must be a no-op for all of them.

- [ ] **Step 7: Commit**

```bash
git add -u && git add patches/baselines/perform/wiring_config.json
git commit -m "feat(engine): config pins accept refs — Setup-evaluated push bindings"
```

---

### Task 3: Bend-swap classification for wired pins

**Files:**
- Modify: `engine/src/patch_loader.cpp` (`bind_wiring` gains a second walk)

**Interfaces:**
- Consumes: `bind_wiring(…)` from Task 2.
- Produces: `vg.bendSwaps` entries for wiring-format patches, with membership
  identical to `build_bindings`' rule.

**Why:** P1 keeps the legacy `PitchBendSource` graft. `build_bindings`
populates `vg.bendSwaps` for exactly the entries whose **freq** curve is
empty — bare targets and vcurve-only targets both
(`engine/src/patch_loader.cpp:822-825`). A converted patch must land in the
same set or the 4 bend/slide baselines diff in Task 5. P3 deletes all of this;
until then it must be reproduced faithfully.

The graph-shape rule that reproduces it exactly: **a wired param pin is a
bend-swap target when the path from that pin down to a
`PerformNode(field=frequency)` passes through no `CurveNode`.** A bare ref to
the PerformNode qualifies; `Combined(PerformNode, CurveNode(velocity))` — the
vcurve-only shape — also qualifies, because the frequency leg of that multiply
is direct. A freq-curve chain does not.

- [ ] **Step 1: Add the JSON-space path walk**

Inside `bind_wiring`, above the per-node loop, add the classifier. It works on
JSON, not on built objects, because the shape question is about which node
types lie on the path and JSON still has the type names:

```cpp
    // True when `startId` reaches a frequency PerformNode along at least one
    // path that crosses no CurveNode. Mirrors build_bindings' graft rule:
    // "empty freq curve", vcurve-only shapes included.
    std::unordered_map<std::string, int> memo;   // -1 unknown, 0 no, 1 yes
    std::function<bool(const std::string&)> direct_freq =
        [&](const std::string& nid) -> bool {
            auto m = memo.find(nid);
            if (m != memo.end()) return m->second == 1;
            memo[nid] = 0;                       // cycle guard: assume no
            auto it = nodeMap.find(nid);
            if (it == nodeMap.end()) return false;
            const auto& n = it->second;
            const std::string t = n.at("type").get<std::string>();
            if (t == "CurveNode") return false;  // a curve on the path disqualifies it
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

Add `#include <functional>` at the top of the file if it is not already there.

- [ ] **Step 2: Emit the bend swaps in the same node loop**

Inside the per-node loop in `bind_wiring`, after the config-descriptor loop,
add the param-descriptor loop:

```cpp
        if (!node.contains("params")) continue;
        for (const auto& desc : consumer->param_descriptors()) {
            if (!node["params"].contains(desc.name)) continue;
            const auto& v = node["params"].at(desc.name);
            if (!v.is_object() || !v.contains("ref")) continue;
            const std::string srcId = v.at("ref").get<std::string>();
            if (!direct_freq(srcId)) continue;   // freq-curve chain: no graft
            auto srcIt = g.valueNodes.find(srcId);
            if (srcIt == g.valueNodes.end()) continue;   // generic pass already threw if real
            // `restore` is what the pin holds on unbent notes — the chain
            // itself, exactly as build_bindings records it.
            vg.bendSwaps.push_back({consumer, desc.name, srcIt->second});
        }
```

Note the deliberate asymmetry: `input_descriptors` pins (CombinedSource's
`source1`/`source2` and friends) are **not** scanned. The legacy graft only
ever touched `param_descriptors` pins backed by a ConstantSource — grafting a
combiner input would be a new behavior, not a reproduction.

- [ ] **Step 3: Build**

Run the Task 1 Step 4 build command.
Expected: succeeds.

- [ ] **Step 4: Prove membership on a bend baseline, before any conversion**

Run: `python tools/null_gate_perform_source.py`
Expected: `196/196 identical`. Still no gated patch in wiring format, so this
must remain a no-op. The real proof is Task 5, where converted bend baselines
must match.

- [ ] **Step 5: Commit**

```bash
git add -u
git commit -m "feat(engine): bend-swap classification for wiring-format pins"
```

---

### Task 4: The paramMap → wiring converter

**Files:**
- Create: `tools/parammap_to_wiring.py`

**Interfaces:**
- Produces: `python tools/parammap_to_wiring.py <in.json> <out.json>` —
  rewrites one patch, exits 1 with a message on anything it cannot express.
- Produces: `python tools/parammap_to_wiring.py --check <in.json>` — exits 0
  if the patch has no `paramMap` (already wiring format or never had one), 2
  if it has one that this tool would convert.
- Consumes: `mforce_cli --dump-descriptors` (`tools/mforce_cli/main.cpp:981`),
  which already emits per-type `configs` straight from the registry. The
  converter shells out to it and caches the result in
  `renders/scratch/descriptors.json`. **Do not hand-copy a config list into
  this tool** — that is exactly how the patch linter cried wolf three times,
  most recently on 59 false findings in run 25.
- Consumed by: Task 5's gate, and eventually the P4 mass migration.

**Emission rules** (mirroring `build_bindings`' matrix in JSON space):

| paramMap entry | emitted wiring |
|---|---|
| bare `"node.pin"` | pin refs `__perf_freq` |
| `{target, curve}` | a `CurveNode` (`logx`, or `loglog` when `interp=="loglog"`) reading `__perf_freq`; pin refs the CurveNode |
| `{target, vcurve}` | a `CombinedSource` multiply of the freq leg and a `linear` CurveNode reading `__perf_vel`; pin refs the multiply |
| `{target, curve, vcurve}` | both, multiply of the curve chain and the vcurve chain |
| target with no `.` | pin name defaults to `"frequency"` (loader rule, `patch_loader.cpp:797`) |
| name != `"frequency"` | dropped, with a printed note — the old engine ignored these too (`patch_loader.cpp:783`) |

"pin refs X" means `nodes[target].params[pin] = {"ref": X}` when `pin` is an
ordinary param, and `nodes[target].bindings[pin] = {"ref": X}` when `pin` is a
**config** on that node's type — decided by the descriptor dump, never by
name matching.

Synthesized nodes are **prepended** to the `nodes` array in dependency order
(`__perf_freq`, `__perf_vel`, curves, multiplies) because `build_graph`
resolves refs against nodes already built, in array order.

- [ ] **Step 1: Write the converter**

Create `tools/parammap_to_wiring.py`:

```python
"""Rewrite a patch's legacy instrument.paramMap as pure graph wiring
(plan_perform_source_p2a.md Task 4).

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
    """{typeName: {"params": [...], "configs": [...], ...}} straight from the
    registry. Cached per run; regenerated whenever the exe is newer than the
    cache, so a rebuilt engine cannot leave a stale map behind."""
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


def convert(doc):
    """Returns (doc, n_entries_converted). Raises ValueError if inexpressible."""
    inst = doc.get("instrument")
    if not inst or "paramMap" not in inst:
        return doc, 0
    pm = inst["paramMap"]

    by_id = {n["id"]: n for n in doc["nodes"]}
    new_nodes = OrderedDict()   # id -> node, prepended in insertion order
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
        """entry: str or {target, curve?, vcurve?, interp?} -> (target, head_id)"""
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
                                 "a config from a pin" % (node_id, ntype))
            # Config targets land in "bindings"; ordinary pins in "params".
            # The registry decides, not a name list.
            slot = "bindings" if pin in desc[ntype]["configs"] else "params"
            node.setdefault(slot, {})[pin] = {"ref": head}
            converted += 1

    del inst["paramMap"]
    doc["nodes"] = list(new_nodes.values()) + doc["nodes"]
    return doc, converted


def main():
    if "--check" in sys.argv:
        doc = json.load(open(sys.argv[-1], encoding="utf-8"))
        has = "paramMap" in doc.get("instrument", {})
        sys.exit(2 if has else 0)
    src, dst = sys.argv[1], sys.argv[2]
    doc = json.load(open(src, encoding="utf-8"), object_pairs_hook=OrderedDict)
    doc, n = convert(doc)
    with open(dst, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=1)
    print("converted %d entr%s -> %s" % (n, "y" if n == 1 else "ies", dst))


main()
```

- [ ] **Step 2: Convert one bare-target patch and read the output**

Pick a simple library patch with a bare paramMap target:

```
python -c "import json,glob; [print(f) for f in sorted(glob.glob('patches/library/**/*.json',recursive=True)) if isinstance(json.load(open(f)).get('instrument',{}).get('paramMap',{}).get('frequency'), str)][:3]"
```
Convert the first one to `renders/scratch/conv1.json` and read the file. The
first node must be `__perf_freq`; the old target node's frequency pin must now
be `{"ref": "__perf_freq"}`; `instrument.paramMap` must be gone.

- [ ] **Step 3: Render both and compare hashes by hand**

```
./build/tools/mforce_cli/Release/mforce_cli.exe <the original patch> renders/scratch/a.wav
./build/tools/mforce_cli/Release/mforce_cli.exe renders/scratch/conv1.json renders/scratch/b.wav
python -c "import hashlib; h=lambda p: hashlib.sha256(open(p,'rb').read()).hexdigest(); a=h('renders/scratch/a.wav'); b=h('renders/scratch/b.wav'); print(a); print(b); print('IDENTICAL' if a==b else 'DIFF')"
```
Expected: `IDENTICAL`. If not, stop here — one patch is far cheaper to
diagnose than 118, and Task 5 will only tell you the same thing louder.

- [ ] **Step 4: Repeat Step 3 for one curve patch and one vcurve patch**

Find them:
```
python -c "
import json,glob
for f in sorted(glob.glob('patches/library/**/*.json',recursive=True)+glob.glob('patches/baselines/**/*.json',recursive=True)):
    pm=json.load(open(f)).get('instrument',{}).get('paramMap',{})
    for n,e in pm.items():
        for x in (e if isinstance(e,list) else [e]):
            if isinstance(x,dict) and 'vcurve' in x: print('VCURVE',f)
            elif isinstance(x,dict) and 'curve' in x: print('CURVE',f)
" | sort -u | head
```
Convert one of each, render, compare. Expected: `IDENTICAL` both times.

- [ ] **Step 5: Commit**

```bash
git add tools/parammap_to_wiring.py
git commit -m "feat(tools): paramMap -> wiring format converter"
```

---

### Task 5: The conversion null gate

**Files:**
- Create: `tools/null_gate_wiring.py`

**Interfaces:**
- Consumes: `tools/parammap_to_wiring.py` (Task 4),
  `tools/null_gate_manifest.json` (frozen at P1).
- Produces: `python tools/null_gate_wiring.py` — converts every manifest patch
  to a temp file, renders it, compares against the P1 hash. Exit 0 = all
  identical.

This is the task that proves P2a. Everything before it is machinery.

- [ ] **Step 1: Write the gate**

Create `tools/null_gate_wiring.py`:

```python
"""Conversion null gate (plan_perform_source_p2a.md Task 5).

Every patch in the P1 manifest is rewritten into wiring format by
tools/parammap_to_wiring.py, rendered, and compared against the hash the SAME
patch produced before PerformSource existed. A converted patch that renders
one byte differently means the wiring format does not say what the paramMap
said.

Patches with no paramMap are converted trivially (no-op) and still rendered —
a no-op conversion that changes the audio would mean the tool is corrupting
files it claims not to touch.
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
        had_pm = "paramMap" in json.loads(src.read_text(encoding="utf-8")).get("instrument", {})
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
Expected: `196/196 identical after conversion (118 carried a paramMap)`,
exit 0.

- [ ] **Step 3: If there are diffs, triage by class before fixing anything**

Group the diffs before touching code — the three risk classes at the top of
this plan have different fixes and a shotgun edit will mask which one fired:

```
python tools/null_gate_wiring.py 2>&1 | python -c "
import sys, json
for line in sys.stdin:
    if not line.startswith('DIFF'): continue
    key = line.split()[1]
    d = json.load(open(key, encoding='utf-8'))
    types = {n['type'] for n in d['nodes']}
    inst = d.get('instrument', {})
    pm = inst.get('paramMap', {})
    flat = [x for e in pm.values() for x in (e if isinstance(e, list) else [e])]
    print(('MULTIPLEX' if 'MultiplexSource' in types else
           'VCURVE'    if any(isinstance(x, dict) and 'vcurve' in x for x in flat) else
           'CURVE'     if any(isinstance(x, dict) and 'curve' in x for x in flat) else
           'BARE'), key)
" | sort | uniq -c
```

- `MULTIPLEX` diffs → risk 2. The clones now pull instead of being pushed;
  check whether `extract_subgraph_json` is pulling the chain into the subtree
  at all, and whether each clone got its own CurveNode.
- `BARE`/`CURVE` diffs on a bend/slide baseline → risk 3, Task 3's
  classifier. Compare `vg.bendSwaps.size()` between the two load paths with a
  temporary `fprintf`.
- Broad diffs across unrelated patches → risk 1, RefSource wrapping. Apply
  the stated fallback: exempt `PerformOut`/`CurveNode` from usage counting in
  `resolve_param`.

- [ ] **Step 4: Re-run both gates**

Run: `python tools/null_gate_perform_source.py` then
`python tools/null_gate_wiring.py`
Expected: `196/196` from each. The first proves the legacy path still works,
the second proves the new format says the same thing.

- [ ] **Step 5: Commit**

```bash
git add tools/null_gate_wiring.py && git add -u
git commit -m "test: conversion null gate — 196/196 identical from wiring format"
```

---

### Task 6: Record what landed

**Files:**
- Modify: `docs/perform_source_design.md` (§7 phases)
- Modify: `docs/autonomy/dsp/BACKLOG.md` (item 20)

- [ ] **Step 1: Update the spec's phase list**

In `docs/perform_source_design.md` §7, split the P2 bullet:

```markdown
- **P2a (format)** — ✓ **LANDED <date>** (plan_perform_source_p2a.md).
  `PerformNode` JSON type (one node per field; all instances resolve to the
  voice's shared `PerformOut`), config pins accept refs as Setup-evaluated
  push bindings, bend-swap membership reproduced from graph shape, and
  `tools/parammap_to_wiring.py`. **Conversion null gate: 196/196
  bit-identical** rendering from converted files. Legacy `paramMap` still
  read, unchanged.
- **P2b (UI)** — PerformNode + CurveNode as editor nodes, knot editor
  (extending the existing Curves table + plot), Curves tab and Mappings
  dialog as derived views, save emits the P2a wiring format. Blocked while
  `mforce_ui.exe` is running.
```

- [ ] **Step 2: Update BACKLOG item 20**

Replace item 20's body in `docs/autonomy/dsp/BACKLOG.md` with:

```markdown
20. **[build] PerformSource P2b (UI) is next** — P1 LANDED 2026-08-18
    (null-gated 196/196); **P2a LANDED <date>** — the wiring format itself:
    `PerformNode` JSON type, config refs in a per-node `bindings` object as
    Setup-evaluated push bindings, bend-swap membership reproduced from graph
    shape, and `tools/parammap_to_wiring.py`. Conversion gate
    (`tools/null_gate_wiring.py`): **196/196 bit-identical rendering from
    converted files**, 118 of them carrying a paramMap. Legacy paramMap is
    still read and is not going away.
    P2b = PerformNode + CurveNode as editor nodes, knot editor (extending the
    existing Curves table + plot rather than a new 2D canvas — Matt
    2026-08-19), Curves tab/Mappings dialog as derived views over the graph,
    and save emitting the P2a format. Note for P2b: UI re-save of patches
    using the Envelope fields (`minValue`/`maxValue`/`nominal`) will DROP them
    until the UI serializer learns them — don't hand-author those fields into
    library patches before P2b. Also pending: mforce_keys is broken against
    the post-ParamSlot engine API (pre-existing breakage) — fix or retire,
    Matt's call.
```

- [ ] **Step 3: Commit**

```bash
git add -u
git commit -m "docs: PerformSource P2a landed — wiring format, 196/196 conversion gate"
```

---

## What P2b inherits

Recorded here so the next plan does not have to re-derive it:

- The wiring format is now fixed and gated. The UI serializer's job is to emit
  exactly what `tools/parammap_to_wiring.py` emits, from the node graph it
  already holds.
- `PerformNode` is patch-mode only (spec §6.4) and throws in NodeGraph mode.
  The editor must not offer it in NodeGraph mode — and NT_PARAMETER's survival
  there is spec §6.4's open question, to settle in P2b.
- The UI's `s_loadedParamMap` stash (`tools/mforce_ui/main.cpp:565`) is the
  thing P2b retires. Load converts it into real nodes; the Mappings dialog and
  Curves tab (`main.cpp:4634`, `main.cpp:4409`) read the graph instead.
- `Envelope.minValue` / `maxValue` / `Stage.nominal` land in the UI serializer
  in P2b. Until then, re-saving a patch that uses them drops them.
- `mforce_keys` is broken against the post-ParamSlot engine API (pre-existing).
  Fix or retire is Matt's call, not P2b's.
