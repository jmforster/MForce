# Refactor gates and architecture reporting — design

Date: 2026-10-04. Status: **two-phase split agreed with Matt the same day;
the details below await his sign-off. Nothing is built.**
Source of the requirement: the 2026-10-02 review
(`docs/audits/2026-10-02-architecture-code-review.md`) and the 10-04
discussion recorded in CLAUDE.md under "Design principles".

## 0. Two phases

The code needs major surgery first. Gates that stop slow accretion would
obstruct surgery (moving a 1,000-line function into its own file would
fail as "new code over the limit"). So the protocol has two phases.

**Phase 1, surgery.** Latitude on the route, a fixed destination, and a
check on arrival.

**Phase 2, keeping it clean.** Everything in section 3, switched on when
the surgery is finished.

| Piece | Phase 1: surgery | Phase 2: after |
|---|---|---|
| Feature work | None, in any lane, until Matt lifts it | Resumes, under the gates |
| Design sign-off | Once, on the target architecture and the deletion list; again only if the destination changes | Per structural change (3.7) |
| Checker | A meter: it reports numbers and never blocks | A gate: the ratchet (3.4) is on |
| Edit hook and pre-commit hook | Off | On |
| Module map | Describes the destination | Enforced |
| Behaviour gates (null gate, engine tests, the UI's headless checks) | Run before and after every cut | Same |
| Independent reviewer and architecture report | At each checkpoint | Per campaign or structural change |

### Latitude in phase 1

- Delete rather than fix: dead code, redundant legacy types, duplicate
  subsystems, broken paths nothing uses.
- Rewrite a unit where that is cheaper than untangling it.
- Break internal interfaces without compatibility shims.
- Large moves in few commits; intermediate states may be ugly.
- No per-change approval, no hooks, no ratchet.
- Work crosses areas whenever a change requires it. Finishing one area at
  a time is a goal, not a rule.

### What stays fixed in phase 1

- **The destination.** One target-architecture document for the whole
  codebase, written at the highest effort setting and signed by Matt
  before cutting starts. It carries the target module map and the list of
  what will be deleted, so Matt can veto anything he wants kept.
- **Behaviour.** The null gate, the engine tests and the UI's headless
  checks pass before and after every cut. The UI has almost no behaviour
  gate, so an inventory of what it does today is written before the UI
  is cut, and Matt skims it.
- **Checkpoints.** Surgery proceeds in checkpoints. Each one ends with
  the behaviour gates green, a commit, the independent review (3.8) and
  an architecture report (3.9) with the meter's before-and-after numbers.
- **Arrival.** Surgery is finished when the tree meets the limits in 3.3
  with an empty or near-empty baseline. Then phase 2 switches on.

### What gets built now

Only the meter (the checker in 3.5 without `--tighten`, `--raise` or any
failing exit code), the module map file format, and the CTest
registration of the existing tests. Section 7 lists the full phase 2
build; it happens when surgery ends.

## 1. Purpose

Three things were missing for six months:

1. Nothing measured the structure of the code.
2. Nothing stopped a session from making the structure worse.
3. Nothing showed Matt the structure, so he could only judge sound.

This design adds:

1. A **checker** that measures structure and refuses any change that makes
   it worse.
2. **Three places it runs**, so no session can skip it.
3. **Documents** that put the design in front of Matt before code is
   written, and report what was built afterwards.

It fixes no existing problem. The campaigns do that. This makes sure they
only move in one direction.

### What it cannot do

A checker measures size, duplication, dependencies and forbidden
constructs. It cannot tell a good abstraction from a bad one that fits
the budget. That judgement stays with two controls that are not
mechanical: Matt's sign-off on the design section (3.7) and the
independent reviewer (3.8).

## 2. Where the code stands today

Measured on 2026-10-04 with a throwaway script over the 164 tracked C++
files under `engine/` and `tools/` (57,853 lines). The real baseline is
whatever the built checker records; these numbers are here to size the
problem.

| Measure | Today |
|---|---|
| Files of 500 lines or fewer | 138 of 164 |
| Files over 600 lines | 21 |
| Files over 1,500 lines | 5: `mforce_ui/main.cpp` 14,658; `composer.h` 2,081; `engine_tests/main.cpp` 1,929; `patch_loader.cpp` 1,900; `wave_evolution.h` 1,552 |
| Files that include two or more third-party libraries | 1: `mforce_ui/main.cpp` includes five (ImGui, GLFW, RtAudio, RtMidi, JSON) |
| Lines using `const_cast` | 10 (7 in `composer.h`) |
| Lines using `dynamic_cast` | 51 (20 in `mforce_ui/main.cpp`) |
| Comment lines carrying a date, backlog id or REVIEW number | about 276 by a loose pattern (132 in `mforce_ui/main.cpp`) |
| File-level mutable variables in `mforce_ui/main.cpp` | 175 by a simple pattern; the review counted 349 with a different rule |
| Engine files that include anything from `tools/` or a UI library | 0 |
| Headers in `core/` that include another engine layer | 0 |

Include directions between the engine's directories are clean except for
two two-way pairs:

- `music/conductor.h` includes `render/instrument.h`, and
  `render/instrument.h` includes `music/pitch_bend.h` and
  `music/pitch_curve.h`.
- `source/fm_source.h` includes `filter/filters.h`, and
  `filter/vibrato.h` includes `source/red_noise_source.h`.

JSON is included directly by two `core/` headers and eight `music/`
headers, not only by the serializer files.

Existing infrastructure: no CTest registration; no git hooks; no Claude
Code hooks in the project; Python 3.11 is installed; no C++ analysis tool
is installed.

## 3. Design

### 3.1 How the pieces fit

```
module map  ─┐
limits      ─┼─>  checker  ──>  pass / fail  (with the offending lines)
baseline    ─┘       │
source tree ─────────┘
                     └──>  delta between two commits  ──>  architecture report
```

- The **module map** says which module every source file belongs to and
  which modules each may include.
- The **limits** are the budgets that apply to new code.
- The **baseline** records every existing violation and its current size.
- The **checker** measures the tree and compares it with all three.

### 3.2 The module map

**What it is.** One file, `tools/structure/modules.json`, listing every
module: its name, a one-sentence purpose, the paths it owns, and the
modules and third-party libraries it may include. A readable version,
`docs/architecture/MODULES.md`, is generated from it; the checker fails
if the generated file is out of date. Matt reads `MODULES.md`.

**Why it exists.** It is the one place where a new class or file becomes
visible as a design change. A source file that belongs to no module fails
the check, so nothing can be added without a line in the map.

**First version.** It describes the directories as they are today.
Campaigns make it finer as they split things.

| Module | Path | Purpose | May include |
|---|---|---|---|
| core | `engine/include/mforce/core` | The ValueSource contract and the primitives every node shares | nothing in the engine |
| source | `…/source` | Nodes that produce sound | core, filter |
| filter | `…/filter` | Nodes that process a signal | core, source |
| render | `…/render` | Instruments, voices, mixing, patch loading, WAV I/O | core, source, filter |
| music | `…/music` | The music model and composition | core, render |
| util | `…/util` | Stand-alone analysis helpers (FFT, signal statistics) | nothing in the engine |
| engine sources | `engine/src/*.cpp` | Each file is assigned to the module of its header | as that module |
| each tool | `tools/<name>` | One executable | engine modules; its own third-party libraries |

Third-party libraries are entries in the same table. JSON may be included
only by files whose job is serialization (`*_json.h`, the patch loader,
the registry's configurator hook) and by tools. ImGui, GLFW, RtAudio and
RtMidi may be included only by `tools/mforce_ui`. When the UI is split,
each of those is narrowed to the one UI module that owns that concern.
That narrowing is the mechanical form of separation of concerns: a file
that draws may not also include the audio driver.

Existing includes that break this table (render including two music
headers, six music headers that are not serializers including JSON) go
into the baseline as
recorded exceptions. Which side of each pair moves is a campaign
decision, not part of this design.

### 3.3 The rules

| Rule | What is measured | Limit for new code | Existing violations |
|---|---|---|---|
| File length | Lines per file | 600 | Recorded; may only shrink |
| Function length | Lines per function body | 80 | Recorded; may only shrink |
| File-level mutable state | Non-constant variables declared at file or namespace level | 0 | Recorded per file; may only shrink |
| Module membership | Every source file is owned by exactly one module | Required | None allowed |
| Include direction | Each `#include` against the map's "may include" | No new edge | Recorded per edge |
| Duplicate blocks | Runs of 8 or more consecutive lines that appear in two or more places, compared after removing whitespace, blank lines and comments | 0 new | Recorded; may only shrink |
| `const_cast` | Lines using it | 0 new | Recorded per file |
| `dynamic_cast` | Lines using it | 0 new in core, source, filter, render | Recorded per file |
| Changelog comments | Comment lines with a date, backlog id or REVIEW number | 0 new | Recorded per file |

The two numeric limits (600 and 80) are proposals; see section 5.

The real-time rule (no allocation, lock or throw on a `next()` path) is
not in this table. It needs a running test, not a text measurement, and
that test is campaign 1's first deliverable.

### 3.4 The baseline and the ratchet

`tools/structure/baseline.json` holds one entry per existing violation:
the file, the rule, and the current value.

- A measured value **above** its baseline entry fails the check.
- A measured value **below** its baseline entry also fails, with the
  message "baseline is stale; run `--tighten`". Tightening rewrites the
  entry to the new, lower value. The baseline therefore always equals the
  real state, and its git diff is a truthful before/after table.
- A violation with **no** baseline entry is new code over the limit, and
  fails.
- An entry can be **raised** only by an explicit command that requires a
  written reason. The reason is stored in the baseline and printed in the
  next architecture report, so Matt sees every exception.

Consequence for daily work: adding three lines to `mforce_ui/main.cpp`
fails unless three lines leave it in the same change. That pressure is
the point.

### 3.5 The checker

`tools/structure/`, written in Python, in five small files:

| File | Responsibility | Depends on |
|---|---|---|
| `measure.py` | Turn one source file's text into numbers (lengths, functions, variables, includes, flagged lines). Pure functions, no I/O. | the function-length tool (section 5) |
| `modules.py` | Load the map; answer "which module owns this file" and "may A include B". | nothing |
| `baseline.py` | Load, compare, tighten and raise baseline entries. | nothing |
| `report.py` | Produce the delta between two commits and render `MODULES.md`. | `measure`, `modules`, git |
| `check.py` | The command line. Wires the four together and sets the exit code. | all four |

Modes of `check.py`:

- no arguments: check the whole tree;
- `--file <path>`: check one file (fast, for the edit hook);
- `--tighten`: lower stale baseline entries;
- `--raise <file> <rule> --reason "…"`: record an exception;
- `--delta <git-ref>`: print what changed since that commit: files added,
  removed and renamed with line counts; type declarations added and
  removed per file; every baseline movement.

Alternative considered: one script. Rejected because the tool that
enforces small single-purpose files should be built that way, and because
`measure.py` as pure functions is easy to test with text fixtures.

### 3.6 Where it runs

| Run point | When | What happens on failure |
|---|---|---|
| Claude Code hook | After every file edit Claude makes to a C++ file | `check.py --file` runs; a failure is returned to Claude as blocking feedback naming the rule and the number |
| git pre-commit hook | Every commit, by anyone | The commit is refused |
| CTest | Every test run | The `structure` test fails |

- The Claude Code hook is the one that acts at the moment the mistake is
  made. The harness runs it; it does not depend on the session
  remembering.
- The pre-commit hook lives in `tools/hooks/` and is switched on per
  clone with one git setting. `.claude/` is git-ignored, so the Claude
  hook is also per-machine setup. One install script does both.
- CTest registration comes forward from campaign 7: `enable_testing()`,
  then `engine_tests`, `test_figures` and `structure` registered with the
  repo root as working directory. That also removes the tests' silent
  dependence on being launched from the repo root.

### 3.7 The design section in every spec

Any change that adds, removes, renames or moves a type or source file,
changes a public interface, or adds an include edge between modules needs
a spec with a design section, and **Matt signs that section before any
code is written**. The sign-off is recorded in the spec with the date.

The section is written in language-neutral terms. For each new or changed
type:

- **Name**, and why that name.
- **Responsibility**, in one sentence.
- **Holds and owns:** what it contains, and for each thing it points at,
  who owns it.
- **Collaborators:** who calls it, what it calls.
- **Inheritance or composition**, and the reason for the choice.
- **Alternative considered**, and why it lost.
- **Module:** where it goes in the map, and any new include edge.

Work that touches no structure (patch and score work, a fix inside one
function) needs no design section. The checker still applies to it.

### 3.8 The independent reviewer

At the end of each campaign a reviewer with a fresh context, which did
not write the code, is given the diff, the module map and the signed
design section. Its brief is a fixed file,
`docs/architecture/REVIEW_BRIEF.md`, and its instruction is to refute.
For each item it answers PASS, FAIL or NOT APPLICABLE with a file and
line as evidence:

1. **Separation of concerns.** Does every new or changed file do one job?
   Does any file now mix drawing, audio, persistence and model?
2. **Single responsibility.** Can each class be described in one sentence
   without "and"?
3. **Dependency direction.** Any include against the map? Any cycle?
4. **Duplication.** Does anything in the diff repeat code that already
   exists anywhere in the tree?
5. **Encapsulation.** Public mutable state? Callers reaching into
   internals?
6. **Ownership.** Is the owner of every pointer and reference member
   clear? Any ownership cycle?
7. **Abstraction fit.** Does the code match the design Matt signed? Is
   each inheritance, composition, variant or template choice the one the
   design gave a reason for?
8. **Real-time rule.** Any allocation, lock, throw or unbounded work on a
   `next()` path?
9. **Names and comments.** Do names say what the thing is? Any changelog
   comment?
10. **Tests.** Is the behaviour in the diff covered by a test?

Its output goes into the architecture report verbatim, followed by what
was done about each FAIL.

### 3.9 The architecture report

One per campaign, and one for any other run that changes structure. It is
written to the lane's `reports/` folder and linked from the lane's
REVIEW file, which is Matt's interface. Length: moderately verbose.

Sections:

1. **Summary.** What changed, in one paragraph.
2. **Types and files.** One entry per type or file that is new, removed,
   moved or renamed. The entry below shows the format; its content is
   invented:

   ```
   NEW  PatchDocument  (tools/mforce_ui/patch_document.h, 210 lines)
     Owns: the node/link/group model of one open patch.
     Used by: NodeEditor, PatchCodec, AudioBridge.
     Depends on: engine SourceRegistry only.
     Design choice: plain value type; nothing derives from it.
     Alternative: a base class with one subclass per patch kind.
       Rejected: there is only one kind.
     Name: "Document" because it is what Open and Save act on.
     Read: lines 38-70.
   ```

3. **Interfaces changed.** Before and after, and the callers touched.
4. **Duplicates collapsed.** How many copies became one, and where.
5. **Metrics.** The output of `check.py --delta`, pasted unedited.
6. **Decisions and rejected alternatives.**
7. **Debt knowingly left**, including every raised baseline entry and its
   reason.
8. **Independent review.** The reviewer's findings and the response to
   each.
9. **Reading guide.** The lines most worth Matt's time.
10. **C++ lessons.** One or two, tied to code in this diff: the idiom
    used, the alternative, the C# or Java equivalent.

Sections 2 and 5 start from generated output. The prose is the session's;
the numbers are the tool's.

### 3.10 A refactor lane

`docs/autonomy/refactor/` with `BACKLOG.md` (the nine campaigns from the
review, in order), `REVIEW.md` and `reports/`, the same shape as the dsp
and comp lanes. Matt's interface becomes three REVIEW files.

### 3.11 Failure modes

| Situation | Behaviour |
|---|---|
| The function-length tool is not installed | The check fails with the install command. It never passes silently. |
| A source file cannot be parsed | That file fails, by name. |
| The Claude hook itself errors | The error text is returned to Claude; the pre-commit hook remains the hard stop. |
| A genuine exception is needed | `--raise` with a reason; visible in the next report. |
| Someone commits with `--no-verify` | CTest still fails. CLAUDE.md already forbids skipping hooks. |

### 3.12 Testing the checker

- Unit tests (Python's built-in `unittest`) for `measure.py`,
  `modules.py` and `baseline.py`, using small text fixtures.
- Known-answer tests against the real tree. The checker must report:
  - `mforce_ui/main.cpp` at 14,658 lines with five third-party libraries;
  - `main()` in that file at about 1,711 lines and
    `draw_properties_panel` at about 1,035;
  - the two two-way include pairs listed in section 2;
  - the ring-out block duplicated in `render/instrument.h` (near lines
    620 and 671), as a duplicate.
- A ratchet test: add a line to a baselined file and confirm failure;
  remove one and confirm the "stale baseline" failure; tighten and
  confirm a pass.

## 4. Changes to the standing documents

- **`docs/autonomy/WORKFLOW.md`:** add the refactor lane; add the rule in
  3.7. The "default-proceed" ground rule for autonomous runs stops
  covering structural changes: an unattended run may not add or move
  types without a signed design section.
- **CLAUDE.md:** two lines pointing at the checker and at this protocol.
- **New files:** `docs/architecture/MODULES.md` (generated),
  `REVIEW_BRIEF.md`, `DESIGN_SECTION.md` (the template in 3.7) and
  `REPORT_TEMPLATE.md` (the sections in 3.9).

## 5. Decisions that are Matt's

1. **The two limits.** 600 lines per file and 80 per function are my
   proposals. Today 138 of 164 files are at or under 500 lines, so 600 is
   not tight for well-shaped code. Lower numbers mean more baseline
   entries, not more work now.
2. **A new tool dependency.** Measuring function length needs something
   that can find where a C++ function starts and ends. My recommendation
   is `lizard`, a small pure-Python package installed with `pip`. Java
   and Node are not on the machine, which rules out the usual
   alternatives, and a hand-written C++ function finder is exactly the
   fragile code this effort is removing. I have not installed it. If it
   fails the known-answer tests in 3.12, the fallback is a brace-matching
   counter in `measure.py`. Every other rule, including duplicate
   detection, is plain text matching and needs no dependency. If the
   known duplicate in 3.12 turns out to differ by renamed variables, the
   duplicate rule moves from line comparison to `lizard`'s token
   comparison.
3. **source and filter as peers.** The map lets them include each other,
   because an FM oscillator using a filter and a vibrato using a noise
   source are both nodes built from nodes. The stricter option is one
   direction only, with the two existing includes recorded as exceptions.
4. **Autonomous runs.** Section 4 takes structural changes out of
   "default-proceed". That slows unattended "go" runs whenever they need
   a new type.
5. **Three REVIEW files.** The refactor lane adds one.

## 6. Out of scope

- Fixing any existing violation.
- The allocation-counting test and the removal of throws from `next()`
  (campaign 1).
- Warnings-as-errors and uniform compiler flags (campaign 7).
- Continuous integration on a hosted service. It becomes worthwhile when
  the repository goes public and would run the same CTest suite.
- Checking the Python tools under `tools/`.

## 7. Build order

1. Install and validate the function-length tool against the known
   answers.
2. Write the module map; generate `MODULES.md`.
3. Build the checker and its unit tests.
4. Record the baseline.
5. Register the three tests with CTest.
6. Add the pre-commit hook, the Claude Code hook and the install script.
7. Write the brief, the two templates, the refactor lane and the changes
   to WORKFLOW.md and CLAUDE.md.
8. Run the independent reviewer on this work and write its architecture
   report. That report is the first one Matt sees, before campaign 1
   starts.
