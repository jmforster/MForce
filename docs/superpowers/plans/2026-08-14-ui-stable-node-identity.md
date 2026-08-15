# UI Stable Node Identity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Node names survive load → edit → save verbatim (chunk 1 of the
2026-08-14 UI usability spec); nodes are renameable in place; layout keeps
working because position keys == names.

**Architecture:** The UI load path already keeps JSON `id` as the node
label (main.cpp:1029) and already persists positions/panning in a
`ui.positions` / `ui.panning` block keyed by those ids (save ~1801, load
~1300). The single defect is that BOTH save paths regenerate ids as
`type_prefix+counter` (save_patch_graph:1546-1555, save_node_graph:
1832-1839), discarding names. Fix: serialize `node.label` as the id,
synthesize unique labels only at node creation, add rename-in-place with
paramMap-stash propagation, and prove it corpus-wide with a
roundtrip+render regression script.

**Tech Stack:** C++/ImGui/imnodes (tools/mforce_ui/main.cpp, single file —
follow its patterns), nlohmann::json, Python test scripts under tools/,
headless `mforce_ui.exe --roundtrip` mode.

## Global Constraints

- Spec: docs/superpowers/specs/2026-08-14-ui-groups-usability-design.md §1.
- UI-only change: touches tools/mforce_ui/main.cpp + tools/*.py only. No
  engine/ edits, so only the mforce_ui target needs rebuilding — but
  Matt's UI may hold `mforce_ui.exe` locked; if link fails, rename the
  running exe (`mforce_ui_locked_YYYYMMDD.exe`) and link fresh, per
  run-18/21 precedent. `mforce_ui.exe --stamp` must exit 0 when done.
- Names are ids: must be unique, must not contain `.` (breaks `node.pin`
  paramMap targets), must not start with `__` (reserved: `__output`,
  `__param_*` position keys, FormantSpectrum `<id>__fN` synthesized
  children).
- The paramMap JSON format does not change. Parameter nodes still exist
  (they retire in chunk 2, separate plan).
- Build: `cmake --build build --config Release --target mforce_ui`
  (mforce_cli only needed for the render-compare script; do not rebuild it
  unless stale).
- Commit after each task; explicit paths only (two lanes share this copy;
  verify branch with `git branch --show-current` before each commit).

---

### Task 1: Save serializes node labels as ids

**Files:**
- Modify: `tools/mforce_ui/main.cpp:228` (GraphNode ctor — unique label at
  creation), `:1546-1555` (save_patch_graph id assignment),
  `:1832-1839` (save_node_graph id assignment)
- Test: `tools/test_stable_roundtrip.py` (new)

**Interfaces:**
- Produces: `unique_node_label(const std::string& typeName) -> std::string`
  (free function, declared before GraphNode, defined after `s_nodes`);
  `sanitize_unique_id(const std::string& want, std::unordered_set<std::string>& used, const std::string& typeName) -> std::string`
  (used by both save paths; Task 2's rename validation mirrors its rules).
- Produces: `tools/test_stable_roundtrip.py <dir>...` — exits 1 listing
  patches whose ids changed across `--roundtrip` or whose roundtripped
  render differs from the original's (Task 3 wires it corpus-wide).

- [ ] **Step 1: Write the failing test**

Create `tools/test_stable_roundtrip.py`:

```python
"""Roundtrip regression: node ids survive mforce_ui --roundtrip verbatim,
and the roundtripped patch renders byte-identically to the original.
Usage: python tools/test_stable_roundtrip.py <patch-or-dir> [...]
Exit 1 on any id change or render mismatch; skips patches that fail to
render in BOTH forms (pre-existing CLI limitation, reported not failed)."""
import hashlib, json, subprocess, sys, tempfile
from pathlib import Path

UI = Path("build/tools/mforce_ui/Release/mforce_ui.exe")
CLI = Path("build/tools/mforce_cli/Release/mforce_cli.exe")

def ids_of(path):
    j = json.loads(Path(path).read_text(encoding="utf-8"))
    return sorted(n["id"] for n in j.get("graph", {}).get("nodes", []))

def render(patch, wav):
    r = subprocess.run([str(CLI), str(patch), str(wav)],
                       capture_output=True, text=True)
    return r.returncode == 0

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def main():
    patches = []
    for arg in sys.argv[1:]:
        p = Path(arg)
        patches += sorted(p.rglob("*.json")) if p.is_dir() else [p]
    bad_ids, bad_render, skipped = [], [], []
    with tempfile.TemporaryDirectory() as td:
        for pt in patches:
            rt = Path(td) / (pt.stem + "_rt.json")
            r = subprocess.run([str(UI), "--roundtrip", str(pt), str(rt)],
                               capture_output=True, text=True)
            if r.returncode != 0:
                bad_ids.append((pt, "roundtrip failed: " + r.stderr.strip()))
                continue
            if ids_of(pt) != ids_of(rt):
                bad_ids.append((pt, f"{ids_of(pt)} -> {ids_of(rt)}"))
                continue
            wa, wb = Path(td) / "a.wav", Path(td) / "b.wav"
            ra, rb = render(pt, wa), render(rt, wb)
            if not ra and not rb:
                skipped.append(pt); continue
            if ra != rb or sha(wa) != sha(wb):
                bad_render.append(pt)
    for pt, why in bad_ids: print(f"ID CHANGE {pt}: {why}")
    for pt in bad_render:   print(f"RENDER DIFF {pt}")
    for pt in skipped:      print(f"skip (unrenderable both forms) {pt}")
    print(f"{len(patches)} patches: {len(bad_ids)} id changes, "
          f"{len(bad_render)} render diffs, {len(skipped)} skipped")
    return 1 if (bad_ids or bad_render) else 0

if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: Run test to verify it fails**

Run: `python tools/test_stable_roundtrip.py patches/library/keys patches/baselines`
Expected: FAIL — `ID CHANGE` lines for every patch with hand-written ids
(e.g. any baselines patch whose ids are `env`/`noise`/`osc1`, renamed to
`Envelope1`/`WhiteNoise1`/...). Record the count; also record any
pre-existing `RENDER DIFF` lines — those are NOT this task's bug, note
them in the commit message if present.

- [ ] **Step 3: Implement stable ids**

3a. Before the `GraphNode` struct (above main.cpp:192), declare:

```cpp
static std::string unique_node_label(const std::string& typeName);
```

In the ctor at main.cpp:228 change

```cpp
GraphNode(const std::string& type) : id(next_id()), typeName(type), label(node_display_name(type)) {
```

to

```cpp
GraphNode(const std::string& type) : id(next_id()), typeName(type), label(unique_node_label(type)) {
```

After the `s_nodes` definition, define:

```cpp
// Node labels ARE the serialized ids (2026-08-14 spec §1: stable
// identity), so every node needs a unique one from birth. Load overwrites
// with the JSON id afterward; this covers UI-created nodes.
static std::string unique_node_label(const std::string& typeName) {
    std::string base = node_display_name(typeName);
    auto taken = [&](const std::string& s) {
        for (auto& n : s_nodes) if (n.label == s) return true;
        return false;
    };
    if (!taken(base)) return base;
    for (int i = 2;; ++i) {
        std::string cand = base + std::to_string(i);
        if (!taken(cand)) return cand;
    }
}
```

3b. Shared id sanitizer, placed just above `save_patch_graph`
(main.cpp:1543):

```cpp
// Serialized id = the node's label, made safe: ids embed in "node.pin"
// paramMap targets (no '.'), "__" is reserved for synthesized keys
// (__output, __param_*, FormantSpectrum "__fN" children), and duplicates
// from a hand-edited file must not collapse two nodes into one id.
static std::string sanitize_unique_id(const std::string& want,
                                      std::unordered_set<std::string>& used,
                                      const std::string& typeName) {
    std::string base = want;
    std::replace(base.begin(), base.end(), '.', '_');
    while (base.rfind("__", 0) == 0) base.erase(0, 1);
    if (base.empty()) base = node_display_name(typeName);
    std::string name = base;
    for (int i = 2; used.count(name); ++i) name = base + std::to_string(i);
    used.insert(name);
    return name;
}
```

3c. In `save_patch_graph` replace the id-assignment loop (1546-1555):

```cpp
    // Assign string IDs to nodes — the label IS the id (stable identity).
    std::unordered_map<int, std::string> nodeIds;
    std::unordered_set<std::string> usedIds;
    for (auto& node : s_nodes) {
        if (node.typeName == NT_PATCH_OUTPUT || node.typeName == NT_PARAMETER)
            continue;
        nodeIds[node.id] = sanitize_unique_id(node.label, usedIds, node.typeName);
    }
```

Leave the `oldToNew` remap block (1572-1602) in place: with stable ids it
is an identity map except when the sanitizer had to rename (collision /
illegal name), which is exactly when the paramMap stash still needs the
remap.

3d. In `save_node_graph` replace the same pattern (1832-1839):

```cpp
    // Assign string IDs — label IS the id (stable identity).
    std::unordered_map<int, std::string> nodeIds;
    std::unordered_set<std::string> usedIds;
    for (auto& node : s_nodes)
        nodeIds[node.id] = sanitize_unique_id(node.label, usedIds, node.typeName);
```

(`std::unordered_set` may need `#include <unordered_set>` — check the
include block at the top of main.cpp.)

- [ ] **Step 4: Build and run test to verify it passes**

Run: `cmake --build build --config Release --target mforce_ui`
(if link fails on a locked exe: rename the running exe, link again)
Run: `python tools/test_stable_roundtrip.py patches/library/keys patches/baselines`
Expected: PASS — `0 id changes`; render diffs no worse than the Step 2
baseline. Then `build/tools/mforce_ui/Release/mforce_ui.exe --stamp`
expected exit 0.

- [ ] **Step 5: Commit**

```bash
git branch --show-current
git add tools/mforce_ui/main.cpp tools/test_stable_roundtrip.py
git commit -m "feat(ui): node labels are serialized ids — save no longer renames nodes"
```

---

### Task 2: Rename-in-place with paramMap propagation

**Files:**
- Modify: `tools/mforce_ui/main.cpp` — new `rename_node()` near
  `sanitize_unique_id`; name field at the top of `draw_properties_panel()`
  (title render at :5061); new `--rename` headless mode next to
  `--roundtrip` (:7644)
- Test: extends `tools/test_stable_roundtrip.py` usage manually (no new
  script) via the `--rename` mode

**Interfaces:**
- Consumes: `sanitize_unique_id` rules from Task 1 (same legality:
  non-empty, no `.`, no `__` prefix, unique).
- Produces: `rename_node(GraphNode& node, const std::string& newName, std::string& err) -> bool`;
  headless CLI `mforce_ui.exe --rename <in.json> <oldId> <newId> <out.json>`.

- [ ] **Step 1: Implement rename_node**

Below `sanitize_unique_id` in main.cpp:

```cpp
// Rename = identity change: the label is the serialized id, so the
// paramMap stash (which references ids as "node.pin" target strings) must
// be rewritten in the same breath. Graph wiring needs nothing — links are
// integer pin ids. ui.positions keys regenerate from labels on save.
static bool rename_node(GraphNode& node, const std::string& newName,
                        std::string& err) {
    if (newName.empty())              { err = "name is empty"; return false; }
    if (newName.find('.') != std::string::npos)
                                      { err = "'.' not allowed (ids embed in node.pin targets)"; return false; }
    if (newName.rfind("__", 0) == 0)  { err = "'__' prefix is reserved"; return false; }
    for (auto& n : s_nodes)
        if (&n != &node && n.label == newName)
                                      { err = "name already in use: " + newName; return false; }
    const std::string oldName = node.label;
    node.label = newName;
    if (oldName != newName && s_loadedParamMap.is_object()) {
        std::string prefix = oldName + ".";
        std::function<void(nlohmann::json&)> fix = [&](nlohmann::json& e) {
            if (e.is_string()) {
                std::string s = e.get<std::string>();
                if (s.rfind(prefix, 0) == 0) e = newName + s.substr(oldName.size());
            } else if (e.is_object() && e.contains("target") && e["target"].is_string()) {
                std::string s = e["target"].get<std::string>();
                if (s.rfind(prefix, 0) == 0) e["target"] = newName + s.substr(oldName.size());
            } else if (e.is_array()) {
                for (auto& t : e) fix(t);
            }
        };
        for (auto& [k, v] : s_loadedParamMap.items()) fix(v);
    }
    s_graphDirty = true;
    return true;
}
```

(Adjust `s_loadedParamMap` / `s_graphDirty` names to the actual globals if
they differ — both appear in the current save path at :1638 and :5100.)

- [ ] **Step 2: Add the Properties name field**

In `draw_properties_panel()`, directly after the title line at :5061
(`ImGui::TextColored(...node->label...)`), add a rename row (skip for
special types):

```cpp
    if (node->typeName != NT_PATCH_OUTPUT && node->typeName != NT_PARAMETER) {
        static int  renameNodeId = -1;
        static char renameBuf[64];
        static std::string renameErr;
        if (renameNodeId != node->id) {
            renameNodeId = node->id;
            snprintf(renameBuf, sizeof(renameBuf), "%s", node->label.c_str());
            renameErr.clear();
        }
        ImGui::SetNextItemWidth(180.0f);
        if (ImGui::InputText("##nodeName", renameBuf, sizeof(renameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue)) {
            if (rename_node(*node, renameBuf, renameErr)) renameErr.clear();
        }
        if (!renameErr.empty())
            ImGui::TextColored(ImVec4(0.9f, 0.4f, 0.4f, 1), "%s", renameErr.c_str());
    }
```

- [ ] **Step 3: Add the headless --rename mode**

Next to `--roundtrip` at main.cpp:7644:

```cpp
    if (argc >= 6 && std::string(argv[1]) == "--rename") {
        s_headless = true;
        ImGui::CreateContext();
        ImNodes::CreateContext();
        register_all_sources();
        try {
            load_graph_from_path(argv[2]);
            GraphNode* target = nullptr;
            for (auto& n : s_nodes) if (n.label == argv[3]) target = &n;
            if (!target) { fprintf(stderr, "rename: no node '%s'\n", argv[3]); return 1; }
            std::string err;
            if (!rename_node(*target, argv[4], err)) {
                fprintf(stderr, "rename refused: %s\n", err.c_str()); return 1;
            }
            save_patch_graph(argv[5]);
        } catch (const std::exception& e) {
            fprintf(stderr, "rename failed: %s\n", e.what());
            return 1;
        }
        printf("rename ok: %s %s->%s -> %s\n", argv[2], argv[3], argv[4], argv[5]);
        return 0;
    }
```

- [ ] **Step 4: Build and verify behavior**

Run: `cmake --build build --config Release --target mforce_ui`
Then on the library piano (its `KSPianoString1` id is real):

```bash
build/tools/mforce_ui/Release/mforce_ui.exe --rename patches/library/keys/Piano_bright.json KSPianoString1 string C:/Users/MATTFO~1/AppData/Local/Temp/claude/C---dev-repos-mforce/f9e76b16-00f4-4b65-ae3a-f7d3ba875a09/scratchpad/pb_renamed.json
```

Expected: `rename ok`. Then verify in the output JSON (grep or python):
(a) node id `string` present, `KSPianoString1` absent; (b) every
`"ref": "KSPianoString1"` is now `"ref": "string"` (there is one — the
graph output); (c) every paramMap target `KSPianoString1.*` is now
`string.*` (there are six: frequency, t60, brightness, dispersion,
inharmGain, detune); (d) `ui.positions` has key `string` (headless save
skips positions — run this check by doing the rename in the live UI later;
for the headless path just confirm no stale `KSPianoString1` string
remains anywhere: `grep -c KSPianoString1 <out>` → 0).
(e) Refusals: `--rename ... KSPianoString1 exc_lp.cutoff ...` → exit 1
"'.' not allowed"; renaming to an existing id → exit 1 "already in use".
Render the renamed file and the original with mforce_cli: byte-identical
WAVs.

- [ ] **Step 5: Commit**

```bash
git branch --show-current
git add tools/mforce_ui/main.cpp
git commit -m "feat(ui): rename-in-place — Properties name field + --rename headless mode, paramMap targets follow"
```

---

### Task 3: Corpus-wide regression + docs

**Files:**
- Modify: `docs/autonomy/dsp/REVIEW.md` (new [try] item),
  `docs/autonomy/dsp/BACKLOG.md` (mark this chunk landed on the spec's
  trail), `docs/superpowers/specs/2026-08-14-ui-groups-usability-design.md`
  (§1 correction: `ui.positions` already existed; layout section not
  needed — if not already amended)

**Interfaces:**
- Consumes: `tools/test_stable_roundtrip.py` from Task 1.

- [ ] **Step 1: Run the roundtrip regression corpus-wide**

Run: `python tools/test_stable_roundtrip.py patches/library patches/baselines`
Expected: `0 id changes, 0 render diffs` beyond the Step-2 (Task 1)
pre-existing baseline; skips only for known CLI-unrenderable patches.
If a render diff appears that was NOT in the baseline, STOP — that is a
regression in this work; diagnose before proceeding.

- [ ] **Step 2: Queue the manual UI checks for Matt**

Add to `docs/autonomy/dsp/REVIEW.md` under Awaiting Matt (new item, [try]):
load Piano_bright → rename `BWLowpassFilter2` to `exc_body` in Properties
→ save → reload: name, wiring, curves and position all intact; duplicate
name and dotted name visibly refused; fresh node gets `TypeN` name,
editable. Note the spec/plan paths and that this is chunk 1 of 3.

- [ ] **Step 3: Commit docs**

```bash
git branch --show-current
git add docs/autonomy/dsp/REVIEW.md docs/autonomy/dsp/BACKLOG.md docs/superpowers/specs/2026-08-14-ui-groups-usability-design.md
git commit -m "docs(ui): stable-identity chunk landed — corpus regression green, manual checks queued"
```
