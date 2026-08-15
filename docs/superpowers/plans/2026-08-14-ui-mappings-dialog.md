# UI Mappings Dialog Implementation Plan (spec chunk 2)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Retire the Parameter node in instrument-patch mode; `s_loadedParamMap`
becomes the single live model, edited in a Parameter-mappings dialog, shown as
green badges on Properties rows (absorbs backlog 3m); the UI's own note render
finally honors curves/vcurves like the CLI.

**Architecture:** Load stops materializing Parameter nodes; save emits the
stash verbatim (rename-remap already maintains it). A new
`apply_param_map(freq, vel)` mirrors the engine's resolution (pin →
ConstantSource set; config → set_config; curve = log-hz/linear-value clamped;
vcurve multiplicative) and replaces the Parameter-node scan in both live
render paths — fixing the existing UI-render-ignores-curves infidelity.
NT_PARAMETER survives ONLY in NodeGraph mode (its synthesized frequency node
is what makes node graphs keyboard-playable — run-23 item 21); it disappears
from patch-mode load/save/menus.

**Tech Stack:** tools/mforce_ui/main.cpp only (no engine edits). Corpus gate:
tools/test_stable_roundtrip.py must stay at 0 id changes / 0 NEW render diffs.

## Global Constraints

- Spec §2: paramMap JSON format unchanged, no file migration.
- Badges: `<frequency>` (bare target) / `<curve>` (curve entry), connected-pin
  green ImVec4(0.5, 0.8, 0.5, 1); widget suppressed while mapped; removing the
  mapping restores the widget (the exc_body dead-end).
- Engine truth for evaluation (patch_loader.cpp:685-780): target `node.pin`
  must resolve to an unwired ConstantSource pin, else a config via
  set_config; curve interpolates log-frequency → linear value, clamped at end
  breakpoints; vcurve linear in velocity, multiplies.
- Only "frequency" is instrument-evaluated at note-on today (instrument.h:174);
  the dialog edits any name but flags others as inert.
- mforce_ui.exe may be locked by Matt's UI: rename-then-link; `--stamp` exit 0
  before finishing. Verify branch before each commit; explicit paths.

---

### Task 1: `apply_param_map` — UI-side evaluation parity

**Files:** Modify tools/mforce_ui/main.cpp — new function near
`stream_envelopes_restore`; replace Parameter scans at the two live render
sites (render_waveforms ~:2798 "Set frequency on Parameter nodes"; the
transport/audio path ~:4470 same pattern).

**Interfaces produced:**
`static void apply_param_map(float freq, float velocity)` — resolves every
`s_loadedParamMap` entry under "frequency"; finds node by label; pins via
`find_input(pin)->constantSrc->set(v)`, configs via
`dspSource->set_config(name, v)` + refresh the node's cached configValues;
`static float eval_curve_logf(const std::vector<std::pair<float,float>>&, float)`.

- [ ] Steps: implement (curve eval copied from engine semantics, log-hz
  interpolation, end-clamped; vcurve multiplicative; bare targets get raw
  freq); replace both Parameter-scan blocks with
  `apply_param_map(freq, velocity)`; keep the blocks' surrounding code
  untouched. Build. Manual probe: Waveforms tab on Piano_bright at C4 —
  exc_lp cutoff must follow the curve (≈1176 Hz behavior: visibly darker
  low-note waveform than raw-freq behavior), C6 brighter. Commit
  "feat(ui): apply_param_map — live render honors curves/vcurves (CLI parity)".

### Task 2: Retire Parameter nodes in patch mode

**Files:** Modify tools/mforce_ui/main.cpp.

- [ ] Load (:1264-1330): delete the create-Parameter-nodes block (stash
  assignment at :1272 stays); delete `__param_` position restore key.
- [ ] Save patch (:1610-1760, :1863): drop paramNodes collection, the
  link-derived targets path and the entry_has_curve gate — paramMap emission
  becomes `remap_entry(s_loadedParamMap)` wholesale (remap still guards
  sanitizer collisions); drop `__param_*` positions.
- [ ] Bare-target pin values: pins previously fed by a Parameter node fall
  back to `pin.defaultValue` emission — confirm the loaded default still
  lands in `pin.defaultValue` (it does: load's numeric-param path) so saves
  are unchanged; corpus gate proves it.
- [ ] Curves tab (:3951-3956, :4017-4026, :4039-4043): paramNames = stash
  keys ∪ {"frequency"}; curve-delete fallback rewrites the entry to its bare
  `target` string(s) instead of consulting Parameter links
  (param_node_link_targets dies with the nodes).
- [ ] Patch-mode creation sites: new-patch template (:781) replaces the
  Parameter frequency node with `s_loadedParamMap = {{"frequency", ...}}`
  targeting the template oscillator; add-node menu hides Parameter in
  PatchGraph mode; patch→node/node→patch conversions (:6005, :6171, :6245)
  and playability synthesis keep NodeGraph behavior, patch side reads/writes
  the stash instead.
- [ ] Properties/canvas special cases for NT_PARAMETER in patch mode become
  unreachable — leave the type + NodeGraph paths intact, guard patch-mode
  paths.
- [ ] Build; corpus gate (`python tools/test_stable_roundtrip.py
  patches/library patches/baselines`) — 0 id changes, 0 NEW render diffs
  (paramMap emission is now verbatim-stash, so roundtrips can only get MORE
  faithful; any patch leaving KNOWN_DIFFS gets removed from the list in the
  same commit). Commit "feat(ui): Parameter node retired in patch mode —
  paramMap stash is the model".

### Task 3: Mappings dialog

**Files:** Modify tools/mforce_ui/main.cpp — menu item + `draw_mappings_dialog()`.

- [ ] Menu: "Parameter mapping..." in the main menu bar (near Curves-related
  items); opens a modal-less window, `static bool s_mappingsOpen`.
- [ ] Table over every stash entry (name | target | curve? | vcurve? | X):
  bare strings and object entries, arrays flattened one row per element.
  "curve" cell: shows breakpoint count; button focuses the Curves window
  (ImGui::SetWindowFocus("Curves")). Delete removes the element (array
  collapse rules as in the Curves tab). Names other than "frequency" get a
  gray "(inert — not evaluated at note-on)" tag.
- [ ] Add row: node combo (pre-select the selected graph node), pin/config
  combo — extract the Curves tab's eligible-target enumeration (:4084-4110)
  into a shared `eligible_targets(GraphNode*)` helper; "Add as bare target" +
  "Add with curve" (seeds a 2-point curve at the target's current value,
  explicit 20/16000 endpoints per the curve-endpoint convention, then focuses
  Curves).
- [ ] Build; exercise via --roundtrip unchanged (dialog is runtime-only).
  Commit "feat(ui): Parameter mappings dialog".

### Task 4: Properties badges (absorbs 3m)

**Files:** Modify tools/mforce_ui/main.cpp — `draw_properties_panel()` pins
loop (:5150-5180) and configs loop (:5195+).

- [ ] Helper `static const char* mapping_badge(const std::string& nodeLabel,
  const std::string& name)` → nullptr / "<frequency>" / "<curve>" by scanning
  the stash for target `label.name` (bare → frequency badge, object-with-curve
  → curve badge).
- [ ] In both loops: if badge, render it in ImVec4(0.5f, 0.8f, 0.5f, 1)
  instead of the InputFloat/slider (tooltip: "driven per-note by the paramMap
  — edit in Parameter mapping / Curves"). Removing the mapping in the dialog
  restores the widget next frame (no state to clear).
- [ ] Build; --stamp exit 0. Commit "feat(ui): green mapping badges on
  Properties rows (closes 3m)".

### Task 5: Regression + docs

- [ ] Full corpus gate green; Piano_bright roundtrip render byte-identical;
  --rename still leaves 0 stale references (curve targets now only in stash).
- [ ] REVIEW 30 [try]: Matt's manual pass — badges on Piano_bright (six
  <curve> rows on the string node, <curve> on exc_lp/exc_clickgain/exc_level
  pins, exc_body flat curve visible in dialog and deletable, slider returns);
  dialog add/delete; node graphs still keyboard-playable.
- [ ] BACKLOG 3m marked absorbed; spec §2 annotated (NodeGraph keeps
  Parameter; UI live render now curve-faithful). Commit docs.
