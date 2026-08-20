# PerformSource P2b Implementation Plan — the editor

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

---

## Read this part first (plain language)

### Where P2a left it

A patch can now say the whole thing in graph nodes: a `PerformNode` reporting
the note's pitch, a `CurveNode` bending it, a wire into a pin, and — for
settings like `t60` or `sustainLevel` — a `dynamicPins` entry that gets pushed
once per note. All 196 patches convert and render byte-identical.

But **nothing in the editor knows any of this exists.** The UI still keeps the
old `paramMap` as a verbatim blob it carries from load to save
(`s_loadedParamMap`, 45 references across 15 functions), and the node graph
still shows none of it.

### What P2b does

Teaches the editor the model, so what a patch *does* is visible on the canvas
instead of hidden in a dialog.

1. `PerformNode` and `CurveNode` become real editor nodes you can see and wire.
2. Load turns a legacy `paramMap` into those real nodes — the blob stops being
   the model.
3. Settings gain **promotion**: a grey pin beside each eligible setting in the
   Settings pane; click it, it turns gold, the value box is replaced by a
   label, and a gold pin appears on the node face.
4. Save emits the P2a wiring format.
5. The Curves tab and Mappings dialog become *views over the graph* rather than
   editors of a blob.

### What it costs you

**Your patch files change format the first time the UI saves them.** That is
the point, and it is the one irreversible-feeling step in the whole phase. The
protection is that saving must not change what a patch *sounds* like, byte for
byte — enforced by a gate over all 196 patches, run after every task.

Two `KSPianoString` nodes in different patches will no longer look identical.
That is correct — they are doing different things — but it is a real change in
what a node is.

### Decided already (don't reopen)

From `pin_model_design.md`, Matt-approved:
- Pins belong to the **patch**, not the type. A setting has no pin until
  something drives it.
- **Float means eligible.** No new metadata.
- A dynamic pin is **set once per note, by definition**.
- A dynamic pin may be fed by **a Curve and nothing else, until we need
  something else** — the editor enforces this, the loader stays permissive, so
  widening it later costs no engine change.
- **Demotion restores the stowed scalar**, not the last value the chain
  produced.
- The knot editor **extends the existing Curves table + plot**. Not a new 2D
  canvas.

### How you'll know it worked

`python tools/test_stable_roundtrip.py patches/library patches/baselines`
stays green. That gate loads every patch through the UI, saves it, renders the
saved file and requires byte-identical audio.

---

## The rest is execution detail

**Goal:** Make the editor speak the pin model — PerformNode/CurveNode as real
nodes, promotion in the Settings pane, Curves/Mappings as derived views, and
Save emitting P2a's wiring format — with **no change to any patch's rendered
audio**, proven by the UI round-trip gate over all 196 patches.

**Architecture:** `s_loadedParamMap` stops being the model. Load converts a
legacy `paramMap` into real `GraphNode`s and `Link`s using the same matrix as
`tools/parammap_to_wiring.py`; save walks the graph and emits `PerformNode` /
`CurveNode` nodes, `params` refs for fixed pins and `dynamicPins` for promoted
settings. Promotion state lives on the `GraphNode` (which settings this
instance drives, plus the stowed scalar), which is exactly what "pins belong to
the patch" means in the editor's data model.

**Tech Stack:** C++20 (MSVC), Dear ImGui + imnodes, nlohmann::json, Python 3.

## Global Constraints

- **The UI round-trip gate is the law.**
  `python tools/test_stable_roundtrip.py patches/library patches/baselines`
  must stay green after every task. `KNOWN_DIFFS` holds exactly one carve-out
  (`FormantSequence1.json`, backlog 3p, a pre-existing UI-vs-CLI gap). **A new
  entry there is a regression — fix the save path, never grow the list.**
- Both engine gates must also stay green:
  `tools/null_gate_perform_source.py` and `tools/null_gate_wiring.py`, 196/196
  each.
- **A running `mforce_ui.exe` is not a reason to change the plan.** Rename the
  locked exe and link the new one (run-19 precedent); if the rename fails, say
  so and ask Matt to close it.
- Verify the branch live (`git branch --show-current`) before each commit —
  two Claudes share this working copy.
- Stage with explicit paths. `git add -u` and `git add <dir>` both sweep up
  files that are not ours (`patch_loader.h`, `mforce_ui/CMakeLists.txt`, five
  vendored third-party repos).
- Commit trailer: `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`

## Method notes carried from P2a (these cost real time there)

- **A check that cannot fail is not a check.** P2a had three steps whose
  verification was "confirm this is a no-op"; all three would have passed with
  a live bug in. Every task below states a check that fails if the change is
  wrong.
- **Never bulk-substitute text in code.** In P2a a sed hit a prefix collision
  (`get_config` inside `get_configurator`) that compiled clean, and another hit
  an indentation substring that duplicated a call. Use anchored edits with
  surrounding context.
- **The gate only sees what patches exercise.** Every new node type is invisible
  to it until a patch uses one. New capability needs a patch that exercises it,
  not just a green gate.
- **Measure, don't infer.** The CurveNode bug was found by rendering a node's
  output directly and reading the number, after three wrong inferences from
  peak levels. Renders carry a mono→stereo ×0.7071 factor; divide it out before
  comparing against theory.

---

## Known blocker, found while baselining (fix in Task 1)

**The converter's `__` prefix collides with the UI's reserved-prefix rule.**

`sanitize_unique_id` strips a leading `__` (`main.cpp:1748`,
`while (base.rfind("__", 0) == 0) base.erase(0, 1);`) and `rename_node` refuses
one (`:1837`, `:1891`), because the UI reserves `__` for its OWN synthesized
keys — `__output`, `__param_*`, FormantSpectrum's `__fN` children.

P2a chose `__perf_freq` / `__curve_N` / `__mul_N` for precisely the same
reason: they are synthesized. So both conventions agree on what `__` means and
disagree on who may write it. Round-tripping a wiring-format patch through the
UI today renames every synthesized node:

```
__perf_freq -> _perf_freq     __curve_bright -> _curve_bright
```

Renders are unaffected (ids are internal), but the round-trip gate fails on id
change, and silently renaming nodes the converter created is exactly the kind
of drift that makes a later diff unreadable.

**Fix in Task 1**, before anything depends on it: the strip exists to stop
*user labels* claiming the reserved namespace, not to reject legitimately
synthesized ids. Teach the UI that a node it created by conversion — or one
whose type is `PerformNode`/`CurveNode` arriving with a `__` id — keeps its id.
The rename refusal at `:1837`/`:1891` should stay: a *human* still must not
type `__`.

Do not solve this by changing the converter's prefix. `__` is the correct
marker and P2a's 196/196 gate is built on those exact ids.

---

### Task 0: Baseline all three gates

**Files:** none (verification only).

- [ ] **Step 1: Handle a locked UI exe**

`Get-Process mforce_ui -ErrorAction SilentlyContinue`; if present,
`Rename-Item build/tools/mforce_ui/Release/mforce_ui.exe mforce_ui_locked.exe`.

- [ ] **Step 2: Build everything**

```
& "C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe" --build build --config Release
```
Expected: every target, exit 0.

- [ ] **Step 3: Record all three baselines**

```
python tools/null_gate_perform_source.py
python tools/null_gate_wiring.py
python tools/test_stable_roundtrip.py patches/library patches/baselines
```

**Baseline taken 2026-08-19, at commit d1206b6:**

| gate | result |
|---|---|
| `null_gate_perform_source.py` | **196/196** manifest entries identical, 3 NEW patches reported, exit 0 |
| `null_gate_wiring.py` | **196/196** identical after conversion (117 carried a paramMap), exit 0 |
| `test_stable_roundtrip.py` | **199 patches: 2 id changes, 0 NEW render diffs, 1 known diff, 77 id-only (non-instrument), 1 skipped** — exit 1 |

The round-trip gate is **red at baseline**, and it is important not to
"remember" it as green. The two id changes are `wiring_smoke.json` and
`wiring_setting.json` — the wiring-format baselines P2a added — failing on the
`__` prefix collision described above, **not on audio**: zero new render diffs.
The other two lines are pre-existing and expected: `FormantSequence1` is
backlog 3p's carve-out, `NATest1` is unrenderable in both forms.

So Task 1's success condition is stronger than "gate still green": it is
**2 id changes → 0**, with everything else unchanged.

---

### Task 1: PerformNode and CurveNode as editor nodes

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — node type constants, `build_pins`,
  `create_dsp`, `node_title_color`, `node_display_name`, the Add-node menu

**Interfaces:**
- Produces: `NT_PERFORM` (`"PerformNode"`) and the registry-backed
  `"CurveNode"` usable as editor node types.
- Produces: `GraphNode::performField` (`0` = frequency, `1` = velocity) and
  `GraphNode::curveKnots` + `GraphNode::curveInterp`, the editor-side state for
  the two types.

**Why first:** every later task manipulates these nodes. Building them first
means Tasks 2–5 have something real to convert into, wire, and serialize.

- [ ] **Step 1: Add the type constants and display treatment**

Alongside the existing `NT_*` constants (`main.cpp:106-111`), add
`NT_PERFORM = "PerformNode"`. `CurveNode` is already in the SourceRegistry
(`source_registrations.cpp:186`), so it needs no constant — but it does need a
`node_title_color` case and an inspector, because its `knots` array is not an
`array_descriptor` and the generic inspector will not show it.

Give both a colour distinct from the existing families (`main.cpp:142-170`);
`PerformNode` in particular should read as a source-of-truth leaf, not a
generator.

- [ ] **Step 2: Give PerformNode its pins and DSP**

`PerformNode` has **one output pin and no inputs**. In `create_dsp`
(`main.cpp:285`), it has no engine object of its own in the editor — the real
adapter belongs to the voice, and the editor is not a voice. Back it with a
`ConstantSource` holding a plausible preview value (440 for frequency, 0.8 for
velocity) so waveform previews and Listen taps do something sensible rather
than emitting zero.

State this in a comment: **the editor's PerformNode is a stand-in.** The engine
resolves it to the voice's shared adapter at load; the editor never has a
voice, so the preview value is a fiction that exists only to make the canvas
render.

- [ ] **Step 3: Give CurveNode its knot state**

`CurveNode` has one input pin (`source`) and one output. Its knots and interp
mode are editor state (`curveKnots`, `curveInterp`), loaded from and saved to
`params.knots` / `params.interp`.

- [ ] **Step 4: Menu placement**

Both go in the Add-node menu. **`PerformNode` must be offered in PatchGraph
mode only** — the engine throws `no voice context` in NodeGraph mode, and
offering a node that cannot load is exactly the silent-failure class this
project keeps hitting. Grey it out with a tooltip naming the reason rather than
hiding it.

- [ ] **Step 5: Verify by authoring, not by inspection**

Build, then hand-author a patch in the UI containing a `PerformNode` →
`CurveNode` → oscillator frequency chain, save it, and render it with
`mforce_cli`. Expected: it renders, and the pitch tracks the note.

This is the check that would fail if the nodes are cosmetic only. A "the menu
shows the node" check would not.

- [ ] **Step 6: All three gates**

Expected: unchanged from Task 0. Nothing has touched load or save yet.

- [ ] **Step 7: Commit**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "feat(ui): PerformNode and CurveNode as editor nodes"
```

---

### Task 2: Load converts paramMap into real nodes

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — `load_graph_from_path` (`:1071`,
  `:1462`)

**Interfaces:**
- Consumes: Task 1's node types.
- Produces: after load, a legacy patch's graph contains real `PerformNode` /
  `CurveNode` / `CombinedSource` nodes and links; `s_loadedParamMap` is left
  **empty** for converted patches.

**Why:** this is the task that retires the blob. The conversion matrix is
already written and proven — mirror `tools/parammap_to_wiring.py` exactly, in
UI graph terms rather than JSON.

- [ ] **Step 1: Port the conversion**

Same matrix, same node naming (`__perf_freq`, `__perf_vel`, `__curve_N`,
`__mul_N`), same rule for `dynamicPins` vs `params` — a target is a setting if
it appears in that type's `setting_descriptors()`, which the UI can ask the
registry directly rather than shelling out to `--dump-descriptors`.

Lay the synthesized nodes out to the LEFT of their consumers with a fixed
offset so a converted patch opens legible instead of as a pile at the origin.

- [ ] **Step 2: Verify conversion equivalence against the proven converter**

For every patch in `patches/library` and `patches/baselines`, compare the UI's
load-converted graph against `tools/parammap_to_wiring.py`'s output for the
same patch: same set of synthesized node types, same count, same targets.

They are two implementations of one matrix; they must agree. Where they differ,
**the Python one is right** — it is gated at 196/196.

- [ ] **Step 3: The gate**

Round-trip gate green. This is the first task where it can genuinely fail: load
now rebuilds the graph, so a conversion bug changes what gets saved and
therefore what renders.

- [ ] **Step 4: Commit**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "feat(ui): load converts paramMap into real graph nodes"
```

---

### Task 3: Save emits the wiring format

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — `save_patch_graph` (`:1912`), which
  currently emits the stash verbatim at `:1971`

**Interfaces:**
- Produces: saved patches carrying `PerformNode`/`CurveNode` nodes, `params`
  refs, and `dynamicPins` — and **no `paramMap`**.

- [ ] **Step 1: Replace the verbatim emission**

`save_patch_graph` currently does "the stash IS the model — emit it verbatim
through the collision remap" (`:1967-1973`). Delete that; emit the graph.

Settings promoted on a node emit as `dynamicPins`; fixed pins emit as `params`
refs, which the existing link-walking code already does.

- [ ] **Step 2: The Envelope fields the serializer still drops**

`Envelope.minValue` / `maxValue` and `Stage.nominal` landed in P1 and the UI
serializer never learned them — the standing warning in BACKLOG 20. Add them
here, since this task is already rewriting envelope emission. Without it, the
first UI save of any patch using them silently discards them.

- [ ] **Step 3: Verify by round-trip, then by second round-trip**

Gate green. Then re-run the gate **on the already-saved outputs** — a format
that is not a fixed point (save→load→save changing the file) will show up as a
second-pass diff even when the first pass looks clean.

- [ ] **Step 4: Commit**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "feat(ui): save emits the P2a wiring format"
```

---

### Task 4: Promotion — grey pin to gold

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — the Settings block in the properties
  panel (`:6285`), node-face pin drawing, `GraphNode`

**Interfaces:**
- Produces: `GraphNode::dynamicPins` — the promoted settings for this node
  instance, each holding the stowed scalar.

- [ ] **Step 1: The promotion control**

A small grey pin button beside every **float** setting in the Settings pane
(`main.cpp:6289` loop). Int/bool/enum settings get none — they are structural.

Clicking promotes: stow the current scalar, mark the setting dynamic, and the
value widget is replaced by a label. Clicking again demotes: **restore the
stowed scalar** and remove the wire.

- [ ] **Step 2: The node face**

Promoted settings appear as gold pins in a dynamic-pin section below the fixed
pins. Gold must be visibly distinct from fixed-pin styling — that is what tells
you the value is frozen for the life of the note, and it is what will explain
why bend moves a string's pitch but not its decay time.

- [ ] **Step 3: Curve-only enforcement**

Only a `CurveNode` may be wired into a gold pin. Refuse the connection with a
message naming why, rather than accepting it and behaving surprisingly. The
loader stays permissive on purpose — this is a UI-level floor so widening it
later costs no engine change.

- [ ] **Step 4: Verify the full cycle**

Author a patch, promote `KSPianoString.t60`, wire a curve, save, render, and
confirm the pitch-dependent decay is audible in the output. Then demote it in
the UI, save, render again, and confirm the render matches a patch that never
had the promotion — **that is the check that demotion restores the scalar**
rather than leaving a stale value behind.

- [ ] **Step 5: Gate, then commit**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "feat(ui): promote a setting to a dynamic pin"
```

---

### Task 5: Curves and Mappings as derived views

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — `draw_mappings_dialog` (`:4634`),
  `draw_curves_window` (`:4409`), `mapping_badge` (`:6114`),
  `curve_exists_for`, `target_exists_for`, `apply_param_map` (`:3266`)

**Why last:** these are the remaining `s_loadedParamMap` consumers. Once they
read the graph, the stash can be deleted outright.

- [ ] **Step 1: Repoint the views**

The Mappings dialog becomes a table of every dynamic pin and perform-rooted
wire in the graph. The Curves window edits the knots of real `CurveNode`s.
`mapping_badge` asks the graph whether a pin is driven.

- [ ] **Step 2: ~~Delete `s_loadedParamMap`~~ — REVISED, it stays**

This step assumed conversion was all-or-nothing. It is not: a paramMap target
pointing into a Formant child owned by a FormantSpectrum cannot become a node,
because the UI consumes those children into the spectrum's row table and a row
holds literal floats. Five voice patches hit this.

So the stash survives, **demoted from the model to the residue** — expected
empty for most patches, small for the rest. Its declaration comment says so.
Deleting it outright needs the formant row model to carry driven params, which
is real work and not this task's.

What DID have to change is every consumer that treated it as the model: the
Curves window and Mappings dialog now derive from the graph and show the stash
only under an explicit "not convertible to nodes" heading.

- [ ] **Step 3: `apply_param_map` retires too**

That function (`:3266`) mirrors the engine's note-on application for UI
playback. With real nodes in the graph, the UI's own DSP path evaluates the
chain — verify keyboard playback still retunes, because this is the path that
made 3n's UI-vs-CLI mismatch invisible for weeks.

- [ ] **Step 4: Gate, then commit**

```bash
git add tools/mforce_ui/main.cpp
git commit -m "refactor(ui): Curves and Mappings derive from the graph; paramMap stash retired"
```

---

### Task 6: Record what landed

- [ ] **Step 1** — `perform_source_design.md` §7: P2b landed, with the gate
  numbers.
- [ ] **Step 2** — `pin_model_design.md`: note that promotion shipped, and
  whether the interaction matched §6 as designed.
- [ ] **Step 3** — BACKLOG 20: close it, and **remove the standing
  `Envelope.minValue`/`maxValue`/`nominal` warning** if Task 3 Step 2 landed.
  That warning has been carried forward for three phases; it should not outlive
  the fix.
- [ ] **Step 4** — Commit.

## What P3 inherits

- The bend hole (`perform_source_design.md` §7 P3, backlog 26a): bend tracking
  is per-node. `WavetableSource` follows a moving frequency, `KSPianoString`
  reads it once in `init_note()` and never again. P3's articulated `.frequency`
  will be silently inert on the latter.
- `paramMap` is still read by the loader and always will be. P2b stops the UI
  *writing* it; it does not remove the reading path.
