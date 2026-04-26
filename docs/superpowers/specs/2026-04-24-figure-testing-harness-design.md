# Figure Testing Harness — Design Spec

**Date:** 2026-04-24
**Context:** Step 2 of ComposerRefactor3 (see `docs/ComposerRefactor3.md`).
**Status:** Draft — pending decisions flagged inline for Matt.
**Predecessor:** Step 1 (WrapperPhraseStrategy) landed on `figure-builder-redesign` (commits `c0415a3`..`cd2491d`). Step 2 builds on that strategy.

---

## Goal

Exhaustively test figures at three layers and establish a listening harness for transform exercises, then retire the ad-hoc CLI test hacks.

Three layers to exercise (from `docs/ComposerRefactor3.md` step 2):

| Layer | Test kind | What is asserted |
|---|---|---|
| `figure_transforms::*` | pure unit test | step sequences in/out |
| Composer locked-figure path | integration | realized `MelodicFigure` matches input unit-for-unit |
| Pitch realization | integration | `elementSequence` Note `noteNumber` matches expected scale walk |
| End-to-end | listen | ear check on transform character |

---

## Scope

Four stages, each independently useful and independently commit-able:

1. **Unit tests.** Pure `figure_transforms::*` coverage in the `test_figures` binary.
2. **Integration tests.** Extend `test_figures` to cover (a) the Composer locked-figure round-trip across multiple figures/variants, (b) pitch realization after scale walk.
3. **Listening harness.** A rendering subcommand (or sibling binary) that produces WAVs + JSON for transform demos — starting with the canonical `fig / invert(fig) / retrograde(fig) / retrograde(invert(fig))` four-phrase passage.
4. **Retire CLI hacks.** Delete `--test-rfb` and `--test-replicate` from `tools/mforce_cli/main.cpp` and their supporting factories (`make_rfb_test_piece`, `figs_*`, `make_replicate_base`, `replicate_cases`, etc.). Delete or archive the committed RFB-golden JSONs under `renders/`.

Each stage ships working software and leaves the tree green.

---

## Pending decisions (Matt)

Defaults assumed below; plan uses these defaults unless Matt overrides.

### D1 — Fate of the RFB goldens in `renders/`

The six `renders/rfb_*.json` files committed in `d6c200b` are tied to `--test-rfb`. Once that subcommand is deleted, the goldens have no driver.

- **Default: delete.** Dead goldens mislead. Commit history preserves them. Simpler tree.
- **Alternative: archive** under `renders/archive/2026-04-23-rfb-goldens/` with a `README.md` pointer. Worth doing only if they have reference value; otherwise noise.

### D2 — Binary layout

How many binaries does step 2 grow?

- **Default: one binary (`test_figures`) with subcommands.** `test_figures` (no args) runs all unit + integration tests, exits 0/1. `test_figures --render <patch> <out_dir>` produces listening artifacts. One executable, clear mode flag.
- **Alternative: two binaries.** `test_figures` strictly for asserts (CI-friendly); `demo_transforms` for listening renders. Cleaner separation at the cost of one more CMake target and one more entry in root `CMakeLists.txt`.

### D3 — Listening-harness goldens

Should the listening subcommand have committed goldens?

- **Default: JSON goldens only, no WAV goldens.** JSON piece dumps are small, diff-able, catch algorithmic regressions. WAV binaries are fragile (float paths, audio library updates) and audio verification is what the ear is for.
- **Alternative: no goldens at all.** Listening output is fresh every run, nothing to diff. Lighter commit weight, but loses regression detection.
- **Alternative: WAV goldens too.** Overkill given the ear is the final arbiter.

### D4 — Transform coverage breadth

`figure_transforms::` currently exposes (per `figure_transforms.h`):

Deterministic: `prune`, `set_last_pulse`, `adjust_last_pulse`, `stretch`, `compress`, `invert`, `retrograde_steps`, `combine` (2 overloads), `replicate` (2 overloads), `replicate_and_prune`, `split`, `add_neighbor`, `add_turn`.

Randomized (take `Randomizer&`): `vary_rhythm`, `vary_steps`, `vary`, `complexify`, `embellish`.

- **Default: unit-test every function.** Pure functions are cheap to cover; one 6–10 line test each. Randomized functions use a fixed seed for determinism and assert on structure (count, net, ranges) rather than exact sequences where appropriate.
- **Alternative: skip `vary/complexify/embellish`** (high-level composites) and cover only primitives. Less coverage, same risk.

### D5 — Integration coverage breadth

- **Default: cover every deterministic transform as a round-trip integration test.** One per op: construct base, apply transform, stuff in `lockedFigure`, run through `ClassicalComposer` + WrapperPhraseStrategy, assert the realized figure matches the transform's output.
- **Alternative: representative subset.** e.g. only `invert`, `retrograde_steps`, `combine`, `replicate`. Faster to write but weaker regression net.

### D6 — Listening passage content

Step 1's motivating exercise was `fig / invert(fig) / retrograde(fig) / retrograde(invert(fig))`. A natural fit.

- **Default: single passage with 4 phrases**, one figure each, rests between, all under WrapperPhraseStrategy. One passage per `--render` invocation. Base figure and patch are CLI args.
- **Alternative: multiple canned passages** (`--render fig-inv-retro-ri`, `--render replicate-variants`, `--render combine-chain`). More coverage, more plumbing, more surface for Matt to tune. Probably overkill for step 2; add as future needs arise.

### D7 — Pitch-realization test: what's the oracle?

To assert on `element.pitch` after a scale walk, the test needs to know the expected pitch sequence.

- **Default: hand-coded expected MIDI note numbers** for a small fixed setup (C major, `startingPitch = C4`, base figure steps `[0, +1, -1, 0]` → expected MIDI `60, 62, 60, 60`). Keeps the test self-explanatory and immune to `PitchReader`/`Scale` refactors (the test spec IS the behavior contract).
- **Alternative: use `PitchReader` in the test itself** to compute expected pitches from starting pitch + steps, then assert the composer produces the same. Tautological — both sides use the same code path, so it only catches disagreement between pipeline and PitchReader, not PitchReader bugs.

Going with the hand-coded oracle — that's a real test.

---

## File layout

Minimal spread. No new headers.

- **Modify:** `tools/test_figures/main.cpp` — grow from single smoke test into full suite (unit + integration + optional render subcommand per D2).
- **Modify:** `tools/mforce_cli/main.cpp` — delete `run_test_rfb`, `run_test_replicate`, and their supporting namespace block; remove dispatch lines in `main()`.
- **Modify or delete:** `renders/rfb_*.json` (per D1).

If D2 splits into two binaries:
- **Create:** `tools/demo_transforms/CMakeLists.txt`
- **Create:** `tools/demo_transforms/main.cpp`
- **Modify:** root `CMakeLists.txt` — add subdir.

---

## test_figures structure (D2 default — one binary)

```
int main(int argc, char** argv) {
    if (argc >= 2 && std::string(argv[1]) == "--render") {
        return run_render(argc, argv);   // listening harness
    }
    // Default: run all tests, exit 0 on success, 1 on first failure.
    int rc = 0;
    rc |= run_unit_tests();
    rc |= run_integration_tests();
    return rc;
}
```

Each test is a free function returning `int` (0 = pass, non-zero = fail, printing diagnostic to stderr). A simple `RUN_TEST(name)` macro that increments a counter and short-circuits to the first failure is sufficient. No external test framework.

---

## Listening harness spec

Invocation (D2 default, D6 default):

```
test_figures --render <patch.json> <out_dir> [seed]
```

Behavior:
1. Construct a base figure with a deterministic RFB call — e.g. `rfb.build_by_count(6)` with a documented seed, or hand-authored steps. **Pending decision D6a:** hand-authored vs RFB-built base — default **hand-authored** (reproducible, no seed dependency on RFB).
2. Derive three variants: `invert(base)`, `retrograde_steps(base)`, `retrograde_steps(invert(base))`.
3. Build a `PieceTemplate` with one section, one part, one passage, **four phrases** each with `strategy = "wrapper_phrase"` and its own locked figure. Short rests between phrases for ear separation.
4. Compose, perform via `Conductor`, render mono → stereo, write `<out_dir>/fig.wav` and `<out_dir>/fig.json`.
5. Print per-phrase summary: figure name, unit count, net step, peak/rms.

Per D3 default: `fig.json` is a candidate for a committed golden. If committed, test_figures' default (no-arg) mode can diff it against a fresh render as an extra regression layer.

---

## Out of scope for step 2

- Multi-figure phrases — step 3 (`DefaultPhraseStrategy` rewrite).
- `FigureSource` / shape-strategy cleanup — steps 4–6.
- Additional listening passages beyond fig/inv/retro/ri — added when needed.
- A full test framework (Catch2/doctest). Plain `if/fail` suffices; upgrade later if coverage grows past ~50 tests.

---

## Success criteria

- `test_figures` (no args) runs silently on success, exits 0; each test logs a single-line PASS/FAIL on stderr via `RUN_TEST`.
- Every deterministic transform in `figure_transforms.h` has a unit test.
- Every deterministic transform has an integration test (Composer round-trip via WrapperPhraseStrategy).
- At least one pitch-realization test asserts on `element.pitch` / `Note::noteNumber` from the realized `elementSequence`.
- `test_figures --render <patch> <outdir>` produces `fig.wav` + `fig.json` for the four-phrase exercise.
- `--test-rfb` and `--test-replicate` are gone from `mforce_cli`.
- `renders/rfb_*.json` goldens are deleted (D1 default) or archived.
- K467 render and composer behavior unchanged — re-baselining not required.
