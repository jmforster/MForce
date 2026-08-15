# UI usability: stable identity, Mappings dialog, Groups — design

Date: 2026-08-14. Brainstormed with Matt (interactive session). Driver: the
Piano_bright hand-tune session surfaced the current UI's limits — generic
node renaming on save, scattered layout, no screen-real-estate answer for
complex patches (imnodes has no canvas zoom, confirmed), and the
rewire-output-to-audition-a-component dance.

Three chunks, deliberately independent, land in this order. No engine/render
changes anywhere in this spec; everything is UI + patch-file metadata.

Out of scope (explicit): canvas zoom (imnodes can't), auto-layout/beautify
(Matt lays out by hand), Group reuse library (planned for, not built — see
§3 format).

## 1. Stable node identity + persistent layout (priority 1)

**Names.** A node's UI name IS the patch JSON `id`, preserved verbatim
through load → edit → save. The current behavior (regenerate `TypeNameN` for
every node on load, write those out on save) is removed. `TypeNameN` remains
only as the default name for nodes created fresh in the UI. Rename-in-place
(Properties panel field; double-click on the node title if imnodes
cooperates). Names are ids, so: enforced unique, and a rename updates every
reference on save — graph `ref`s, `instrument.paramMap` targets
(`node.pin`), layout keys, group membership (§3).

**Positions.** RESOLVED DURING PLANNING (2026-08-14): a patch-level
`ui.positions` / `ui.panning` block already exists — written on save
(keyed by serialized node id, plus `__output` / `__param_*`), restored on
load, ignored by the CLI loader and linter (proven: Piano_bright carries
one and lints PASS). No new `layout` section needed; stable names make the
existing keys stable. The `groups` section (§3) joins the same root-level
`ui`-style tolerance and must be verified the same way in the chunk-3
plan.

## 2. Parameter node retired → Mappings dialog

> LANDED 2026-08-14 evening (chunk-2 plan). Two annotations from
> implementation: NT_PARAMETER survives in NodeGraph mode only (its
> synthesized frequency node is what makes node graphs keyboard-playable);
> and the UI's own note render now evaluates curves/vcurves with engine
> parity (apply_param_map) — previously it pushed raw note frequency into
> curve-bearing pin targets, a silent UI-vs-CLI mismatch.

The `instrument.paramMap` JSON format does NOT change. No file migration;
old patches load unchanged. What changes is representation and editing:

- The UI no longer materializes Parameter nodes (NT_PARAMETER removed).
  paramMap was always instrument-block data; the fan-out node depiction is
  what crosses every group boundary and what forced the verbatim-stash save
  path. Both problems disappear with it.
- **Parameter mapping… menu item** opens the Mappings dialog: one table of
  all bindings in the patch. Row = source (`frequency` | `velocity`) →
  target node (dropdown) → target pin/config (dropdown, populated from that
  node's descriptors) → optional curve / vcurve (shape editing stays in the
  Curves tab; the row links to it). Add/remove rows. If a graph node is
  selected when the dialog opens, the node dropdown pre-selects it.
- **Properties badges** (absorbs backlog 3m): any pin or config targeted by
  a mapping shows `<frequency>` / `<velocity>` / `<curve>` in the
  connected-pin green, with its slider/field suppressed — the mapping stomps
  the scalar at every note-on, so a live-looking widget is a lie. Removing
  the mapping restores the widget (the exc_body dead-end from 2026-08-14).
  Badges appear in both panel sections (pins and Settings), since mappings
  can target either species.

## 3. Groups

> LANDED 2026-08-15 (chunk-3 plan). Implementation notes: boundary links
> render as projections onto the collapsed node's synthetic pins (the link
> keeps its real id, so wiring/deleting through a projected pin edits the
> real edge); a group with no outward wire takes its topologically last
> output-bearing member as the Listen output; the tap is applied via
> find_output_source (in-UI renders) and a graph.output override written
> ONLY into the playback temp file.

**Concept.** A Group is UI-level structure over a FLAT graph: the patch
JSON keeps every node top-level; a `groups` section records membership.
The engine never knows groups exist; renders are byte-identical with the
section present or stripped.

```json
"groups": [ { "name": "Excitation", "members": ["noise", "hammer", ...],
              "pos": [x, y] } ]
```

Members may include other group names (nesting supported; breadcrumb
handles depth). Format is shaped so a group can later be lifted into a
reusable library definition (Matt: wants this eventually — plan for it,
don't build it).

**Creation.** Select nodes → right-click → **Group**. Inverse: **Ungroup**
(non-destructive round-trip). The Output node cannot be grouped.

**Interface = boundary wires (auto-derived, never authored).** Every wire
entering the selection from outside becomes an input pin on the collapsed
Group node (named for the internal pin it feeds); all wires leaving the
selection must originate from ONE internal node — that node is the Group's
output. If two internal nodes feed outside, refuse with their names
("selection has 2 outputs: X, Y"). Piano expectation: Excitation
zero-in/one-out, Strings one-in/one-out.

**Drill-in.** Double-click a Group → the Node Editor pane's content swaps
to the group interior (no second OS window). Breadcrumb bar:
`Piano_bright ▸ Excitation`, click any ancestor to navigate up. Adding
nodes inside a group works exactly like top level (covers build-from-
scratch without a "drag into container" mechanism, which imnodes can't
express anyway).

**Listen toggle (in the breadcrumb bar).** Right-aligned on the
breadcrumb line: `Patch | Group`, active side highlighted in the same
green as connections/badges (green = what's sounding, one consistent
signal); clicking the label flips it. Semantics: in-context tap, always —
the whole patch keeps running exactly as wired (keyboard drives it,
mappings apply); the toggle only chooses where the monitored audio is
tapped: the group's output node vs the patch output. Literal isolation
(inputs disconnected) is explicitly NOT a mode — for a group with inputs
it's silence, not a use case (Matt). For zero-input groups (damper) tap
and isolation coincide, so nothing is lost. Drill-in auto-selects Group,
with per-group session memory: first entry to a group defaults to Group;
if flipped to Patch, re-entering that group keeps the last choice.
Transport, QWERTY keyboard, Spectrum and Waveforms all follow the tap
point.

**Tap mechanics.** The UI's playback path already serializes a temp patch
for the CLI-loader engine graph; the tap overrides which node is the
serialized `graph.output` while leaving the instrument block intact. Zero
engine work expected; if the live-audio path taps differently (RtAudio
graph), same principle — monitor source is a node pointer, not a rewire.

**Generalization.** Right-click any node → **Listen here** — same tap
mechanism, no group required; drill-in is just an implicit Listen here at
the group output. The tapped node shows a small speaker marker on the
canvas; taps are exclusive (Listen here elsewhere moves the tap); the
same menu item reads **Stop listening here** on the tapped node and
restores the patch output. Kills the rewire-output-to-hear-the-damper
dance everywhere.

## Interactions between chunks

- §3 depends on §1 (group membership keyed by stable ids) and is much
  simpler after §2 (no Parameter fan-out crosses boundaries; only real
  signal wires define interfaces). Land 1 → 2 → 3.
- Rename updates layout keys, group membership, paramMap targets (§1 rule)
  — single "rename node" code path owns all four.

## Verification expectations

- Round-trip: load → save with no edits is content-identical for every
  library/baseline patch (modulo added `layout` section on first save);
  extend the headless `--roundtrip` mode to assert it corpus-wide.
- Null render: `layout` + `groups` sections stripped vs present →
  byte-identical WAVs (engine blindness proven, not asserted).
- Boundary derivation: unit cases — zero-input, one-input, multi-inbound
  (dedup to distinct pins), two-output refusal message, nested group.
- Mappings dialog: editing a binding then saving reproduces the same
  paramMap JSON the hand-written form uses (Piano_bright as fixture).
- Lint: teach the patch linter the two new sections (validate ids exist;
  no unknown-key false positives — it cried wolf three times already).

## Open items deferred (recorded, not designed)

- Group reuse library (instantiate "Excitation" across patches, per-
  instance overrides) — format is lift-ready; design when wanted.
- Visual curve editor (Curves tab still edits point lists) — unchanged by
  this spec; badges/dialog link to it as-is.
