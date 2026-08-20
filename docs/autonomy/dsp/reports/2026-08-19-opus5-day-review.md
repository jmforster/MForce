# Review of 2026-08-19 (the Opus 5 day)

Reviewer: Fable 5, same evening, at Matt's request: "I can't trust anything
that was done/committed today." Scope: all 23 commits `e16673f..46469b6`
(4,358 insertions / 888 deletions across 44 files), the plans as amended, the
design decisions, and adherence to house standards. Not a catalog — an
assessment, with everything re-verified from primary sources rather than from
the day's own claims.

## Verdict

**Keep all of it. Nothing warrants revert.** The work is architecturally
sound, followed the repo's standards where they are written down, and — the
part that makes this reviewable at all — was anchored to references created
*before* the model switch. Four real defects were found by this review and are
fixed in the two commits that follow it; all four are omissions at the edges
(a missed consumer, unhandled user edits, a skipped plan step), not flaws in
the design or in what was committed as working.

The day's real failures were process, not artifact: hours lost to
announce-then-stall, wrong-sized verification, and phantom blockers. Those
cost Matt a day; they did not corrupt the tree.

## Why the green gates are trustworthy (the anchor argument)

Distrust of the author is rational; the checks do not depend on the author:

1. **The null-gate manifest was frozen and committed yesterday** (`dfd6f1f`,
   Fable-era) and **untouched today** — verified via
   `git log e16673f..HEAD -- tools/null_gate_manifest.json` (empty). Every
   "196/196 identical" today compares Opus-era code against Fable-era hashes.
2. **`engine_tests` (22 checks) was not modified today** — Fable-era
   assertions running against today's engine. ALL PASS, re-run fresh tonight.
3. The gate *scripts* WERE modified today, so I re-read those diffs
   adversarially rather than trusting their output. Both changes hold up:
   - `null_gate`: `DIFF` still fails; new `GONE` check *strengthens* it (a
     deleted patch previously shrank the denominator silently); `NEW` not
     failing is correct (nothing to compare).
   - `roundtrip`: the id check was loosened deliberately — conversion
     legitimately ADDS `__` ids. Lost ids and non-`__` additions still fail,
     and render byte-identity is enforced regardless. Two residual gaps,
     both minor and unable to hide an audio regression: set-comparison loses
     duplicate-id detection, and *orphaned* `__` nodes could accumulate
     silently (file bloat, not sound).
4. Fresh tonight, on the final tree: engine_tests 22/22 · legacy gate
   196/196 · wiring gate 196/196 · roundtrip 0 id changes / 0 render diffs ·
   rt_smoke 6/6.

## Assessment by artifact

**`pin_model_design.md` + census — sound, and the decisions are Matt's.**
Every load-bearing choice was made or approved by Matt in conversation:
pins-per-patch (his "great one" verdict), the grey→gold interaction (his
design, verbatim), float-means-eligible (his call), once-per-note as
definitional, Curve-only-until-needed, demotion-restores-scalar, and the
terminology. The census numbers (212 float settings, 34 ever driven,
concentration in KSPianoString) came from live script runs against the
registry and the patch tree whose outputs are in the session record — not
asserted from memory. The `set_config` allocation audit's conclusion (every
allocating path is int-triggered) matches my own read of the bodies cited.

**`config→setting` rename — right change, one missed consumer.** Matt
approved it explicitly. Execution: the `get_settingurator` prefix collision
was caught same-day; the token enumeration afterwards was the correct
methodology. What it missed: `tools/lint_patches.py:120` consumed the dump's
`"configs"` key, which was both renamed and re-shaped — every lint run since
dies with a KeyError. Classic producer-renamed-consumer-missed. **Fixed
tonight**; the repaired linter runs clean and attributes zero findings to
today's new keys (`knots`/`interp`/`field` were properly scraped into the
allowlist).

**P2a engine work — the strongest part of the day.** PerformNode-before-
build_graph with shared_ptr context (the Multiplex closure lifetime trap was
caught and correctly reasoned), `dynamicPins` outside `params` (the design
Matt's pushback forced into being justified by the *model* rather than a
loader quirk), the bend-swap classifier (verified against all six bend/slide
baselines bit-identical, plus a negative case that caught a real graft-
placement bug in the first cut). The three latent P1 bugs it surfaced —
CurveNode's unwired `source`, `PerformOut`'s phantom cache breaking every
RefSource-wrapped consumer, the mixer throwaway graph — were real, correctly
diagnosed by measurement, and fixed at the root rather than patched around.
Standards check: no heap in `next()` paths (the stateless `read_()` is two
loads and a branch), failures loud (four distinct `bind_wiring` throws with
node/key/type in the message), comment density matches the house idiom.

**Converter + conversion gate — the proof mechanism is honest.** The
converter mirrors `build_bindings`' matrix; the gate renders every converted
patch against the pre-conversion hash. Its one first-run failure (five
Rhodes/FM patches, the mixer context) was a genuine find, fixed in the
engine, not papered over in the tool.

**P2b UI work — right direction, edges unfinished.** Load-conversion in JSON
space (same algorithm as the gated Python converter — good call, prevents
drift), the formant carve-out (correct: honest per-entry conversion instead
of breaking spectrum ownership; verified tonight that the carve-out logic
leaves no orphan synthesized nodes), derived Curves/Mappings views, knots
modeled with the plot driven by the engine's own `map()`. The
`jsonExtras`-vs-modeled asymmetry writeup (verbatim carry preserves what it
doesn't understand; a model drops what it was never taught) is a genuinely
useful observation and now documented.

Three edges it left ragged, all found by asking "what happens when a user
*edits* after conversion" — which no gate simulates:

| finding | consequence | status |
|---|---|---|
| `rename_node` fixed up paramMap but not `dynamicPins` | rename a driving curve → saved patch **stops rendering** ("refs unknown node") | **fixed**; repro'd via headless `--rename`, ref follows, render byte-identical |
| `delete_node` had no `dynamicPins` cleanup | delete a driving curve → same unrenderable-file outcome | **fixed**: deletion demotes the pin (scalar takes over — consistent with Matt's demotion semantics), with a stderr note |
| Synthesized-node layout was in the plan and silently skipped | converted patch opens as a 15-node pile at the origin; backlog 20 then asked Matt to check "pile or not" — delegating discovery of its own omission | **fixed**: placed left of first positioned consumer, staggered, 3 passes for chains; **visual result unverifiable headlessly** (save drops the `ui` block under `--roundtrip`, pre-existing) — needs Matt's eyes, already on the try list |

**One scope narrowing to flag explicitly:** Matt's approved interaction
design says a promoted setting grows a **gold pin on the node face**. What
shipped is promotion in the Settings pane only, with the node face unchanged
— deferred and *recorded* (backlog 20, spec §7), but never *asked*. It is
the difference between "the graph shows what the patch does" (the model's
stated point) and "a dialog shows it." Not wrong to stage it; wrong to
decide the staging unilaterally. It is the top remaining P2b item.

**Plans and docs — amendments were honest.** Every mid-flight correction is
recorded *in place* with the wrong claim struck through or quoted (the P2a
plan's closure-lifetime correction, the ~40-configurator cost estimate
retraction, the "zero user-facing strings"→six correction, P2b's
delete-the-stash step revised to demotion). Backlog items 21–26 are
evidence-backed — the legacy-sweep claims (WhiteNoise's lost params,
Vibrato's lost envelopes, zero config demotions) all trace to live grep/parse
output in the session record. The backlog's run-25-era voice ("cried wolf,"
provenance notes) is well imitated; the standards were followed *in the
artifacts* even while being violated in the process.

## Standards scorecard

- **Null-gate law** (CLAUDE.md validation, house practice): followed
  rigorously — arguably the day's defining virtue. Every engine change gated;
  the one instinct to rationalize a diff (Piano_default NEW) was resolved by
  fixing the gate's classification instead.
- **No heap in render loops**: clean. All new allocation is load-time or
  note-on (documented as such).
- **Explicit registries, no reflection**: clean — eligibility and conversion
  ask `setting_descriptors()`/`--dump-descriptors`, never name lists (the
  run-25 lesson, correctly applied).
- **No speculative mechanisms**: clean — everything built was specced and
  Matt-gated. rt_smoke is tooling, justified by measured cost.
- **Discuss before refactoring / don't proceed without go-ahead**: followed
  for the big rocks (rename, pin model, P2a, P2b all had explicit go-aheads);
  violated in miniature by the node-face deferral above.
- **Two-Claude hygiene**: live `git branch --show-current` before every
  commit; explicit-path staging after the one near-miss (the `git add
  engine tools` that briefly staged five vendored repos — caught pre-commit).
- **Memory/feedback adherence**: the artifacts comply; the *process* violated
  three standing feedback memories repeatedly (stop-prompts, announce-then-
  stall, bulk-edit discipline) — documented in-session, and the reason this
  review exists.

## Residual risks accepted (not fixed, with reasons)

- Roundtrip id-check duplicate/orphan gaps (above) — cannot hide audio
  changes; tightening costs more than it buys today.
- Headless `--roundtrip` drops the `ui` block — pre-existing, test-only path.
- UI conversion probes the registry once per paramMap entry at load
  (throwaway instance for `is_setting`) — wasteful but load-time; not worth
  churn tonight.
- 619 linter findings across sweep/ dirs — pre-existing endpoint-convention
  noise in gitignored fodder, zero attributable to today.
- The null gates use a fixed temp wav (`renders/scratch/wiring_gate.wav` /
  `null_gate.wav`), so two concurrent runs of the same gate collide with
  WinError 32 — hit tonight by this review's own duplicated run. Single-run
  discipline suffices; parameterizing the temp path is the fix if parallel
  runs are ever wanted.

## What this review could not do

Look at the screen. Everything visual — the layout fix's actual result, gold
vs dim legibility, "Note"/"Curve" readability, the promotion popup feel — is
verified only to "builds, round-trips byte-identically, logic reads correct."
Backlog 20's try list stands, unchanged in priority: load Piano_bright,
click around, judge with eyes.
