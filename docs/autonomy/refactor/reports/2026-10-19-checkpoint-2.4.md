# Architecture report: checkpoint 2.4, taps reading B

Part 2 of the target architecture, checkpoint 2.4 — commits `a8d5aa9`..`403269f` on
main, 2026-10-19 (the fix wave `403269f` on `557fc01`). Plan: `docs/superpowers/plans/2026-10-19-part2-checkpoint-2.4.md`
(its Ledger holds every ruling). Spec: `docs/architecture/part2-valuesource-contract.md`
section 2 "Taps". Measurement: `docs/architecture/measurements/2026-10-19-same-tick-taps.md`.
The format is the one agreed at checkpoint 0 (report R1).

## 1. Summary

A tap is one sample late wherever it sits. Until this checkpoint the tap edge's
one-sample delay was positional: a tap read whatever its source held at the
moment the tap was evaluated, which was last tick's value when the tap sat on
the cycle's forward path (every loop skeleton) and this tick's value — no delay
at all — when the tap sat where a plain wire would have been legal (the brass
and bore loops whose output is taken downstream of the tap's source). Now:

```cpp
float ValueSource::previous() const;   // the value this node held at the end of the previous tick
float TapSource::compute() override { return guard_(source ? source->previous() : 0.0f); }
```

Every node keeps the value it held at the end of the previous tick (`prev_`)
beside this tick's (`cur_`); `next()` and `step()` move `cur_` into `prev_` at
the moment they claim the tick, before the sweep and the compute, so a tap read
before, during or after its source's compute gets one answer. The tap's
`current()` — the read that delivers, since a node reads its pins after its
sweep through `current()` — is the memo once the tap has served this tick and
the live source value before it is pulled (a Setup-time read between ticks,
where the present value is what the coming tick's tap delivers). No new verb,
no pin change, no loader
change, no UI change; the two-value memo landed first unused and byte-identical
(`b0a1a5f`), and the tap's read switched in its own commit (`557fc01`), the
one commit whose renders change. `DelayLineSource`'s `compensate` assumption —
one sample from the closing tap — now holds by rule for a cycle with one tap,
where before it held by position (and did hold, on every compensated loop in
the sample: their closing taps all sit mid-compute); its comment changed, its
code did not.

Renders changed in one measured class and nowhere else in the 1,615-patch
sample (section 5, Appendix A): 40 patches in which a tap read a value its
source had already computed in the same tick. In 37 of them the tap is a
control read, not the loop's closing tap: the nonlinear-bore block's
`NL_curve ← tap(NL_in)` driving `NL_delay.ratio` (the trombone, onemass and
nlbore families) and the pierce model's `SignCurve ← tap(Delay_line)` driving
`Damp_lpf.cutoffFreq`; that modulation now lags its input by one sample and
the loop periods are unchanged. In the three `stk_blowhole_port` patches a tap
between two tail delay lines (`D1In ← tap(D2)`) is on the signal path and that
path gains one sample. All are in audition, pending, sweep and old — none in
library, none in baselines, so the null gate's manifest was never refrozen
(78/78 after every task). The measured list of 45 was an upper bound by five,
each explained. The class is larger on disk than in the sample: 116 patches
carry one of the three shapes (section 5), among them
`audition/articulation1/trombone_tongue`, which the CLI A/B could not exercise
because it has no score.

Render time: neutral — the three library patches at 1.01x, 0.99x and 0.99x
of the pre-2.4 binary, the nine micro patches between 0.98x and 1.04x except
the 24-millisecond one at 0.95x, which is the noise floor at that length
(section 5). One float store per node per compute, as the plan priced it.

## 2. Types and files

```
CHANGED  ValueSource  (engine/include/mforce/core/dsp_value_source.h)
  Owns: the memo, now two values — `cur_` (this tick's) and `prev_` (the value
    at the end of the previous tick) — beside `lastTick_`; previous().
  Used by: TapSource (the one reader of previous()); every node inherits the
    store.
  Design choice: `prev_ = cur_` at the moment next() or step() claims the
    tick, before the sweep and the compute. A reader that arrives before the
    claim gets cur_ (still last tick's value); one that arrives mid-compute
    gets prev_, which just took the old cur_ (the same number); one that
    arrives after the store gets prev_ (last tick's). Three read positions,
    one answer. previous() is non-virtual and const: a read of the memo, not
    a pull, and not a view's live forward.
  Alternative: the tap keeps the delay itself (a value written at the end of
    each tick). Rejected: a tap has no end-of-tick hook, and giving it one
    means a per-tick list of every tap kept by the loader — the kind of
    machinery checkpoint 2.3 deleted. Alternative: prev_ written after the
    compute from a saved local. Rejected: a reader that arrives mid-compute
    (every loop closed through a tap on the node whose forward path reaches
    the tap) would see a value two ticks old, and every loop patch would move.
  Name: "previous" — the value of the previous tick, in both branches of the
    expression; "prev_" after it. Not "last" (ambiguous with lastTick_) and
    not "delayed" (the delay is the tap's, the value is the node's).
  Read: next(), step() and previous() near the top of the file.

CHANGED  TapSource  (engine/include/mforce/core/dsp_value_source.h)
  Owns: a shared_ptr to its source; guard_() (non-finite to 0, ±8), static,
    applied to both reads.
  Design choice: compute() reads source->previous() — the per-tick read,
    memoised by the tap's own next() like any node's. current() is the read
    that delivers: a node reads its pins after its sweep through current()
    (checkpoint 2.1's rule), so once the tap has claimed this tick it answers
    with the memo, and next()'s return and the post-pull current() agree.
    Before it is pulled it answers live with the source's present value.
    Between ticks (a Setup-time read) the clock holds at the last rendered
    tick, so a never-pulled tap answers live — the value the coming tick's
    tap delivers — while a tap pulled in that tick still answers with its
    memo, one sample older than the coming delivery; a Setup-time step()
    through such a tap reads the same memo (section 7; the refutation's I3).
    One guard for both reads, because the guard is the edge's armour and not
    a property of either read.
  Alternative: current() always live (the plan's contract). Rejected by the
    first A/B arm: every consumer read the live value and reading B reached
    nobody — 47 of 47 tap patches byte-identical. Alternative: no current()
    override (the base's cur_). Rejected: a tap never pulled this tick (a
    trigger source read at Setup) would serve a memo from some earlier tick,
    or 0, where the live value is the right one. Alternative: current()
    always previous(). Rejected: between ticks the source has claimed the
    tick that just ended, so a Setup-time reader would get a value two ticks
    old.
  Name: read_() became guard_() because it no longer chooses what to read;
    claimed_tick() on the base names the one fact both previous() and the
    tap's current() turn on.
  Read: the struct, twenty-five lines; claimed_tick() beside sample_rate().

CHANGED  DelayLineSource  (engine/include/mforce/source/delay_line_source.h), comments only
  The `compensate` setting shortens the read by `comp = 1` for the closing tap
    plus each loop member's phase delay. The comment called that sample
    "positional"; it is now "one sample, by rule". The code is unchanged: the
    assumption held by position before and holds by rule now for a cycle with
    one tap; on every compensated loop in the sample the closing tap sits
    mid-compute, so it held there under both readings (section 5, ruling 10).

MEASURED, GONE  the same-tick tap counter  (a8d5aa9 only)
  `ValueSource::computed_this_tick()`, two static inline members on TapSource
    and one count in its compute(); an RAII printer at the top of
    mforce_cli's main() (`[taps] sameTickReads=N` on every exit path);
    `tools/measure_taps.py` driving the eight set files. All deleted in the
    next commit; `git show a8d5aa9:tools/measure_taps.py` rebuilds the
    measurement. Lesson 3.
```

## 3. Interfaces changed

| Before | After | Callers touched |
|---|---|---|
| — | `float ValueSource::previous() const` | one reader, `TapSource::compute()` |
| `TapSource::read_()` (chose what to read, then guarded) | `TapSource::guard_(float)` (static; guards what it is handed) | `compute()`, `current()` |
| `TapSource::compute()` reads `source->current()` | reads `source->previous()` | the tap edge's per-tick read; nothing else |
| `TapSource::current()` reads `source->current()` live, always | the memo once the tap has claimed this tick, live before | every consumer of a tap (its post-sweep pin read); a Setup-time reader of a never-pulled tap unchanged, of a tap pulled in the ended tick the memo (section 7) |
| — | `bool ValueSource::claimed_tick() const` (protected) | `previous()`, `TapSource::current()` |
| `ValueSource::operator=` copies `cur_` | copies `cur_` and `prev_` | `Envelope::replace_stages` (unchanged) |

Nothing in the loader, the UI, the pins or any node changed. The tests gained
two sections (`run_previous_tests`, `run_tap_reading_tests`) and one assertion
of 2.3's live-read test moved from reading A to reading B (section 6).

## 4. Duplicates collapsed

None. `read_()`'s two callers already shared one guard; they still do.

## 5. Metrics

`python tools/structure/check.py --delta fc7ad11` at the seal (head `403269f`):

| Measure | Before | After | Change |
|---|---|---|---|
| files | 167 | 167 | +0 |
| lines | 59,011 | 59,169 | +158 |
| files_over_limit | 20 | 20 | +0 |
| functions_over_limit | 99 | 99 | +0 |
| file_level_mutable_vars | 114 | 114 | +0 |
| const_cast | 10 | 10 | +0 |
| dynamic_cast | 57 | 57 | +0 |
| changelog_comments | 252 | 252 | +0 |
| duplicate_blocks | 156 | 157 | +1 |
| include_violations | 13 | 13 | +0 |
| unmapped_files | 0 | 0 | +0 |

The core header grew by 32 lines (`prev_`, `previous()`, `claimed_tick()`, the
tap's two reads and their comments), `delay_line_source.h` by two lines of
comment, the test file by 124 (two sections and the pinned limits). No type was
added or removed. The duplicate-block figure moved by one because the test
file's existing twin blocks shifted by the inserted lines and the tool
re-paired them: the sixty block pairs before and after are the same blocks at
new line numbers; nothing new is duplicated.

The renders that changed. One class, measured before the cut
(`docs/architecture/measurements/2026-10-19-same-tick-taps.md`) and confirmed by
the A/B over the same sample (Appendix A): **a tap read a value its source had
already computed in the same tick**, and now reads the value the source held
at the end of the previous tick. Three shapes, by family:

- **The nonlinear-bore block** (`trombone1`-`4`, `onemass_lip1`, `nlbore_probe1`,
  `nlbore_brass1`, `Trombone_att1`): `NL_delay` is a DelayLine whose `ratio` is
  `NL_curve`, a CurveNode reading the delay's own input `NL_in` through a tap.
  `Processor::input` is registered before `ratio`, so `NL_delay`'s sweep
  computes `NL_in` through `source` before `ratio` reaches the tap: a same-tick
  read. The ratio modulation now uses the previous sample's input. The loop's
  closing taps (`Dp`, `Pe`, `Pplus` on `Bore`) sit inside `Bore`'s own pull,
  mid-compute, where readings A and B agree; the loop period is unchanged and
  `compensate` (on, on `Bore`; off, on `NL_delay`) is irrelevant to the change.
- **The pierce model** (`pierce1d_1`): `Damp_lpf.cutoffFreq` is `SignCurve`,
  reading `Delay_line` through a tap after `Damp_lpf`'s input has computed it;
  the damping cutoff now follows the previous sample. The closing tap
  (`DIn ← tap(Damp_lpf)`) is mid-compute. `compensate` is on but inert: no tap
  closes to `Delay_line` through input pins, so the loop walk finds no cycle.
- **stk_blowhole_port**: `D1In ← tap(D2)` between two tail delay lines
  advanced in file order (`D2` before `D1`): a signal-path tap that was
  same-tick and is one sample late now; that path gains one sample.
  `compensate` is off.

This corrects the plan's and the first draft's story — "the loop gains the
one sample the tap was always said to provide" — which the refutation (C1)
showed to be wrong for 37 of the 40: the closing taps of every compensated
loop in the sample are mid-compute under both readings.

The 40, by family (patches that differ / patches of the family on the list):

| Family | Differ / listed | The same-tick read | Where |
|---|---|---|---|
| `audition/pierce1d_1` | 6 / 6 | `SignCurve ← tap(Delay_line)` → `Damp_lpf.cutoffFreq` | Matt's listening queue |
| `audition/trombone1` | 4 / 4 | `NL_curve ← tap(NL_in)` → `NL_delay.ratio` | Matt's listening queue |
| `pending/brass` (`Trombone_att1`) | 1 / 1 | the same | Matt's sandbox |
| `sweep/trombone1`, `2`, `3`, `4` | 2+3+4+2 / 11 | the same | sweeps |
| `sweep/onemass_lip1` | 4 / 7 | the same (the three `_st0` controls: flat knots, did not move) | sweeps |
| `sweep/nlbore_probe1` | 6 / 7 | the same (`nlbore_oboe_ctl`: flat knots, did not move) | sweeps |
| `old/nlbore_brass1` | 5 / 6 | the same (`nlb_lip_lad000`: flat knots, did not move) | archive |
| `sweep/stk_blowhole_port` | 3 / 3 | `D1In ← tap(D2)`, a signal path | sweeps |

In numbers: `trombone_attempt4` peak 0.89986 → 0.899911, rms 0.139945 →
0.139424. The extent on disk: 116 patches carry one of the three shapes — 101
with `tap(NL_in)` (sweep trombone1-4 35, `nlbore_probe1` 20, `onemass_lip1` 20,
`old/nlbore_brass1` 20, audition 5, pending 1), 6 in `pierce1d_1` (its sibling
`pierce1d_2` taps `Pierce` mid-compute only, no `SignCurve`: not in the class,
in the sample, identical), 9 in `stk_blowhole_port` — against 45 listed and
40 confirmed in the sample, whose
`sweep` and `old` sets are every third patch. The un-sampled sweeps of those
families, and `audition/articulation1/trombone_tongue` (the NL block with
knots 1.24 / 0.76 and no score, so the CLI renders it without notes and the
A/B is vacuous for it), will sound different when played.

The list against the A/B: the measurement listed 45; 40 differ. The counter
sits at the tap's `compute()` position, which is not quite the delivery
position: a same-tick delivery created by a later pin of the same consumer's
sweep (the source computed after the tap was pulled but before the post-sweep
`current()` read) would count 0. The A/B over the same sample is the proof
that none exists there — every differing patch is on the list and none is off
it (the refutation's I2).
The five that did not move — `old/nlbore_brass1/nlb_lip_lad000`,
`sweep/nlbore_probe1/nlbore_oboe_ctl`, `sweep/onemass_lip1/lip_r080_loud_st0`,
`lip_r100_soft_st0` and `reed_r080_loud_st0` — are the control variants of the
nonlinear-bore family and are not silent (peak 0.22 to 0.88). Their only
same-tick tap feeds `NL_curve`, a CurveNode whose knots in those variants are
all 1.0: a flat curve whose output does not depend on its input, so the read
value is never used. Their siblings with knots 1.06 / 0.94 (`_st1`, `lad030`)
moved. A same-tick read of a value the consumer does not use changes nothing,
which is the sense in which the list is an upper bound.

The two patches on the 2026-10-18 list that no longer count
(`stk_bowed_ext1/ext_tors_c10`, `stk_bowed_ext2/ext2_larson_btd`) are explained
in the measurement document: their bow chain's old advancer sat inside a
tap-only tail, so the tap read its source after the root pull had advanced it;
under the memo the root pull computes the chain inside the source's own
compute, the mid-compute position where readings A and B agree. The A/B
confirms it: both are identical.

Render time (`python tools/ab_render_time.py renders/scratch/ab_tree/cli_pre24.exe
build/tools/mforce_cli/Release/mforce_cli.exe 7`; "speedup" = base / new):

| patch | base wall (ms) | new wall (ms) | speedup | base render (ms) | new render (ms) |
|---|---|---|---|---|---|
| oboe1 | 863.0 | 857.6 | 1.01x | 21.5 | 21.3 |
| piano_default | 1063.0 | 1073.2 | 0.99x | 41.9 | 41.7 |
| viola_default | 1308.3 | 1318.3 | 0.99x | 6.0 | 6.3 |
| A sine | 487.0 | 478.4 | 1.02x | 136.4 | 132.4 |
| B sine+env | 498.5 | 489.5 | 1.02x | 150.1 | 150.3 |
| C filter x8 | 803.0 | 799.8 | 1.00x | 452.3 | 444.7 |
| D 8 sines | 24.4 | 25.6 | 0.95x | — | — |
| E shared ref | 551.7 | 530.6 | 1.04x | 187.0 | 182.7 |
| F 40 sines | 3054.3 | 3115.6 | 0.98x | 2809.8 | 2794.4 |
| G 40 own env | 3896.0 | 3896.0 | 1.00x | 3642.2 | 3645.5 |
| H 40 shared | 2943.4 | 2904.8 | 1.01x | 2691.4 | 2664.6 |
| I 40 envs | 1729.4 | 1730.3 | 1.00x | 1488.3 | 1483.4 |

Minimum wall time of seven alternating runs per patch, taken on an idle
machine after the A/B. Everything is within the run-to-run noise of the
method (plus or minus two percent on the second-long patches; D is 24
milliseconds, where one millisecond is five percent). The checkpoint adds one
`float` per node and one store per compute; nothing else touches the sample
path. The 2.1 cost accepted at R4 item 0 stands unchanged behind these
numbers (oboe1 0.73x, piano_default 0.88x of pre-2.1).

## 6. Decisions and rejected alternatives

Each is a ruling in the plan's Ledger, in the order made.

1. **Two values per node, moved when the tick is claimed.** `prev_` takes
   `cur_` at the moment `next()` or `step()` claims the tick, before the sweep
   and the compute. Rejected: the tap keeping the delay itself (no end-of-tick
   hook; a per-tick tap list is 2.3's deleted machinery) and `prev_` written
   after the compute (a mid-compute reader — every loop skeleton — would see a
   value two ticks old). Cost: one float per node, one store per compute.
2. **`previous()` is a non-virtual read of the memo.** The stateless views
   (`NameGate`, `PerformOut`, `TapSource`) override `current()` to answer live
   because a Setup-time read between ticks must see the present; their
   previous value is what they computed last tick, which the sweep guarantees
   they did (with one hole, a view on a trigger pin: section 7). No stored
   patch taps a view (the measurement's scan a: 0).
3. **No clock: `previous()` is `cur_`.** Every pull computes and "this tick"
   has no meaning; the clockless fallback keeps reading A, bare-node tests
   keep their values, and a clockless pull inside a host stays 2.5's fault
   flag.
4. **`current()` on the tap stays the live read.** Between ticks the source's
   present value is the value it held at the end of the last tick — exactly
   what the tap delivers at the next tick; the trigger-chain and push-binding
   reads of 2.3 are unchanged. Superseded by ruling 18 for the within-tick
   read, and true between ticks only for a never-pulled tap (the
   refutation's I3; section 7).
5. **`step()` moves the value the same way.** A tap inside a stepped subgraph
   is one step late when its source steps first, two when the tap steps first
   (section 7; the refutation's Review Focus 4), supported only as far as
   `step()` is (its two stated limits stand; R4 item 3, BACKLOG row 9a). No
   stored patch has a tap in a fill's excitation (scan b: 0 of 62) or in a
   push chain (scan c: 0 of 134).
6. **Landing order.** The two-value memo first, written and never read —
   byte-identical, 78/78 (`b0a1a5f`); the tap's read switched in its own commit
   (`557fc01`), the one commit whose renders change, five lines of engine
   code. Target-architecture principle 6 kept at commit granularity.
7. **The list was measured again, with the sealed binary.** A counter in
   `TapSource::compute()` (a source that has claimed this tick and finished
   its compute — the one position where readings A and B differ), committed
   in `a8d5aa9` and deleted in `b0a1a5f`, so the measurement is reproducible
   from history and nothing of it survives a seal. The 10-18 list of 47 was
   the cross-check: 45 entries at exactly half (the old trace counted the
   wrapper's read twice per sample, sweep and post-sweep `current()`; the
   counter counts `compute()` once per tick), two dropped with the mechanism
   named (section 5). The A/B rule is 2.3's ruling 16: every differing patch
   is on the list and explained; the list is an upper bound.
8. **The old arms were re-used.** The engine was unchanged since `fdf7536`
   (`git diff --stat` over engine and tools: empty), so 2.3's `final` arms are
   the pre-2.4 arms of the six tree sets; only `taps.txt` (the 47 in full, so
   no every-third sampling hides one) and `retired.txt` got a fresh old arm.
9. **No manifest refreeze.** None of the measured patches is in `patches/library`
   or `patches/baselines`; the null gate reported 78/78 after every task.
10. **`DelayLineSource::compensate` keeps `comp = 1`.** The setting subtracts
    one sample for the closing tap: true by position under reading A — and
    in fact true on every compensated loop in the sample, whose closing taps
    sit mid-compute — and true by rule under reading B for a cycle with one
    tap. The comment changed, the code did not. The planning ruling's reason
    (that the assumption was false on the listed patches) was wrong: their
    same-tick reads are control reads, not closing taps, and `compensate` is
    irrelevant to the change (section 5; the refutation's C1). A second tap on
    the same cycle is one more sample the setting does not subtract (M2;
    section 7).
11. **The design documents record the delta, not a rewrite.**
    `feedback_loop_design.md` keeps section 2's "positional" paragraph as the
    08-31 design and says in its header what changed and when; Part 2 section 2
    records the decision. No dated changelog in code.
12. **The retired pair stays in the A/B** (`old.txt`, `retired.txt`): the
    Junction loop skeleton it carries is still rendered by both arms though it
    left the gate (R4 item 2). Both identical.
13. **(execution, Task 0) Gates at step 0 not re-run.** They ran this session at
    `a3c7c2e` and the engine, tests and tools were byte-identical at the A/B
    base `fc7ad11` (docs-only commits between); every later commit ran them.
14. **(execution, Task 0) The two dropped patches are not a counter defect.**
    Re-running the old binary (`cli_pre23.exe`, `MFORCE_TRACE_SHARING=1`) on
    `ext_tors_c10` reproduces the 10-18 numbers (lagReads 1,588,530, same-tick
    1,059,020); the wiring explains them (section 5). The list for the A/B is
    the 45; if either dropped patch had differed it would have been a bug.
15. **(execution, Task 2) 2.3's live-read test asserted reading A at the
    tap-after-source position.** `run_tap_reads_live_tests` (the fix wave of
    R4) pinned `tap->next() == 2` after `c->next()` in the same tick — exactly
    the position this checkpoint changes; the plan wrongly said the test held
    under both readings. The one assertion moved to reading B (1.0, the
    comment naming the position); the live `current()` assertions stand. The
    test now states the rule the checkpoint lands.
16. **(execution, Task 2) The plan's "second assertion fails" was three.** The
    harness continues past a failed CHECK, so the three tap-after-source
    positions of `run_tap_reading_tests` all showed (tick 1, tick 3, the
    step); nothing else failed. The RED was the right shape.
17. **(execution, Task 3) `git grep positional -- engine` is not empty**: it
    matches "Compositional" in `music/structure.h`. `git grep -w` is the
    honest check and lists nothing.
18. **(execution, Task 2) The plan's contract for `TapSource::current()` did
    not deliver reading B.** The first A/B arm over the 47 tap patches was
    byte-identical to the base. Cause: a node reads its pins after its sweep
    through `current()` (2.1's rule; `CombinedSource`, `Biquad`, `CurveNode`
    all do), so the tap's live `current()` override — not its `compute()` —
    is what every consumer uses, and `previous()` reached nobody. Decided:
    `current()` is the memo once the tap has claimed this tick and the live
    source value before (the Setup-time case), through a new protected
    `claimed_tick()` on the base. Restated test-first: a `CombinedSource`
    summing `c + tap(c)` reads 1, 3, 5 (the live read gave 2, 4, 6), and the
    post-pull `current()` reads are the memo. Ruling 4 above is corrected for
    the within-tick read, and its between-ticks half holds for a never-pulled
    tap only: a tap pulled in the tick that just ended answers a Setup-time
    reader with its memo, one sample older than the coming delivery, and a
    Setup-time `step()` through it reads the same memo (the refutation's I3;
    pinned in `run_tap_reading_tests`; section 7). No stored patch reads a
    tap at Setup (0 on trigger pins, 0 in push bindings).

## 7. Debt knowingly left

- **`previous()` is public on every node for one reader**, the tap edge. A
  narrower door (a friend, or the pins owning the read) belongs to Part 3's
  pins; REVIEW R5 item 1 asks whether to narrow it now.
- **A Setup-time read through a tap already pulled in the tick that just
  ended serves the memo** — one sample older than the coming tick's delivery —
  because the clock holds at the last rendered tick and `start()` does not
  reset the memo; a Setup-time `step()` through such a tap reads the same memo.
  Inside a fill, a tap stepped before its source reads two steps late (the
  source re-claims the same tick, so `previous()` cannot tell a fresh step from
  an earlier one). Pinned as limits in `run_tap_reading_tests`; no stored patch
  has a tap on a trigger pin, in a push binding or in a fill (0, 0, 0 of 62).
  The fix is Part 3's `pin.value(t)`, which retires the live `current()` on the
  tap, or a step generation on the clock; not improvised here.
- **`step()`'s two limits** (a Manual pin or an owned sub-source inside a
  stepped subgraph serves the memo for the rest of the fill; a node on two pins
  of the subgraph steps once per pin) stand; BACKLOG row 9a.
- **`compensate` counts one closing tap.** A second tap on the same cycle is
  walked through by the loop walk and, under reading B, is one more
  deterministic sample the setting does not subtract (under A it was 0 or 1 by
  position). No stored compensated patch has a two-tap cycle.
- **A clockless host pull reads A** — `previous()` is `cur_` with no clock —
  and stays silent until 2.5's fault flag, as every clockless pull does.
- **A tap on a stateless view reads the view's memo**, not its live forward;
  correct under the rule, unexercised by any stored patch (scan a: 0). The
  tail-list guarantee behind it has a hole: `collect_advance_ids` counts an
  `Envelope.trigger` edge as a normal consumer, so a view wired to a trigger
  and tapped elsewhere is neither a tail nor pulled per tick, and its tap would
  read a frozen memo (M3). A load-time warning when a tap targets a node no
  per-tick pull reaches is the real fix, later.
- **The roundtrip gates** still fail on the six backlog-79 patches
  (`double_advance_{ok,bug}`, `loop_tap_{counter,starved}`,
  `perform/wiring_setting`, `wiring_smoke`); pre-existing, Part 4's codec.
- Carried unchanged from 2.3's section 7: the clockless-pull fault flag, the
  hand-wired tap-free cycle's overflow in prepare/start, `computing_` after a
  throw, the `fprintf` on the cycle path, the public pin `source` member,
  shared `Slot` parts advancing twice, `advanceList`'s name, mixer-mode
  cross-loop sharing, the meter's live fixture. The 2.1 render-time cost is no
  longer debt: R4 item 0 accepted it.

## 8. Independent review

# Refutation R5: checkpoint 2.4, taps reading B (a8d5aa9..557fc01, base fc7ad11)

Read-only review of `git diff fc7ad11 557fc01` (engine, tests, docs), `a8d5aa9` for the counter, the plan, the execution ledger, the measurement document, the report draft sections 1-7, and the A/B hash arms on disk. Nothing was built, rendered or run except read-only scans of the patch JSON. Line numbers are HEAD (`557fc01`) unless a commit is named.

### The brief, item by item

| # | Item | Verdict | Evidence |
|---|---|---|---|
| 1 | Separation of concerns | PASS | `dsp_value_source.h` still owns the memo and the tap edge only; no new include (diff adds none; :1-10 unchanged). The place it could have failed — the Task 0 printer in `tools/mforce_cli/main.cpp:1218-1225@a8d5aa9` and the statics on `TapSource` — is gone: `git grep computed_this_tick\|MFORCE_TRACE_TAPS\|sameTickReads\|TapTracePrint -- engine tools` is empty. |
| 2 | Single responsibility | PASS, with a flag | `ValueSource`: "computes once per tick and remembers this tick's and the previous tick's value" — one sentence. `TapSource` now needs a clause: "a one-sample-late read of its source, served live until the tap has been pulled this tick" (`:259-261`). That is one job with two faces, not two jobs; but the second face exists only because of 2.1's read-after-sweep rule (Issue I3). |
| 3 | Dependency direction | PASS | No include changed in `engine/`; `core` includes `core` only (`dsp_value_source.h:2-10`). Tests include `core`/`source` per `MODULES.md` (tests → core, source). |
| 4 | Duplication | PASS | The tick predicate appears once (`claimed_tick()` :140) and feeds both `previous()` :104-106 and `TapSource::current()` :260; one `guard_` :266-269 for both reads. `prev_ = cur_` at :70 and :88 is two one-line stores, one per entry point, not a copy. |
| 5 | Encapsulation | PASS, with debt | `previous()` is a `const` read (:104) of a value the signed design makes a contract fact ("each node also keeps the value of the tick before, and a tap reads that"), so public is the honest door; the only reader is `TapSource::compute` (:253; `git grep previous() -- engine` lists definition + one reader + tests). `claimed_tick()` is protected (:140) and exposes one bool of memo state to every derived node for one subclass's use; a `friend` would be worse (base naming a derived type). Carry as R5 item 1. |
| 6 | Ownership | PASS | Nothing new is a pointer: `prev_` is a `float` (:177). `TapSource::source` (public `shared_ptr`, :245) is pre-existing and carried from 2.3's section 7. |
| 7 | Abstraction fit | FAIL (narrow) | The pull path matches the signed design (Part 2 §2 "B, exact"): :70, :104-106, :253. The `TapSource::current()` hybrid (:259-261) is not in the signed design or the plan's contract (plan "The contract" :152 had current() live always); it was decided in execution (ruling 18) and is a faithful adaptation to 2.1's read-after-sweep rule. But "a tap is always a one-sample delay" is not delivered on the `step()` path or at Setup: see Issue I3 — a push chain stepped at Setup (`instrument.h:368-369`, `:520-521`) through a tap delivers the value two ticks old, the exact alternative the ledger rejected. |
| 8 | Real-time rule | PASS | `previous()` :104-106, `claimed_tick()` :140, `TapSource::current()` :259-261 and `guard_` :266-269 are compares and returns; no allocation, lock, throw or I/O. The `fprintf` on the cycle path (:171) is pre-existing and listed in 2.3's section 7. |
| 9 | Names and comments | FAIL (minor) | Engine comments are clean: no changelog, backlog id or REVIEW number in the diff (`delay_line_source.h:95-98`'s "backlog 66b" is pre-existing and untouched). But `tools/engine_tests/main.cpp:200-203` ("The positional z-1 the whole feedback subsystem rests on") and `:320-321` ("the tap's positional z^-1") now contradict the rule; the plan's Task 3 step 1 check is `git grep positional -- engine tools` and it lists both, while the ledger's ruling 17 only grepped `-- engine`. |
| 10 | Tests | PASS, with gaps | Reading B is pinned through a real consumer's read path — `CombinedSource` summing `c + tap(c)` reads 1, 3, 5 via `source2.current()` after its sweep (`main.cpp:2262-2274`; `combined_source.h:45-46`), not only through `TapSource::next()`. Every expected value in `run_previous_tests` (:2217-2260) and `run_tap_reading_tests` (:2262-2318) was traced by hand against `next()`/`step()`/`previous()`/`claimed_tick()` and holds (details under Review Focus). Uncovered: the `step()` tap-before-source ordering (Issue I3c), a Setup-time `step()` through a tap (I3b), `operator=`'s `prev_` copy (:161; inert, Minor M4), and `comp = 1` under a clock (`run_loop_compensation_tests` :318 is clockless, so it exercises reading A). |

### Review Focus, item by item

1. **Tap on a source that already computed this tick.** Pinned: `main.cpp:2279-2281` (`c->next()` then `tap->next() == 0`), `:2286-2290` (tick 3), and through a consumer at `:2270-2274`. Verified by hand: after `c->next()` c has `lastTick_ == now`, `prev_ = 0`, `cur_ = 1`; `tap->next()` → `compute()` → `c->previous()` → `claimed_tick()` true → `prev_` = 0.
2. **Tap on a source mid-compute.** Pinned: the clocked self-counter `:2302-2309` (1, 2, 3) and the `Probe` reading `mid->previous()` from inside `mid`'s compute `:2240-2252` (sees 0 then 1, not two-old). Verified: at the claim `prev_ = cur_` (:70) runs before `compute()` (:73), so mid-compute `prev_ == cur_ ==` last tick's value. The null gate's 78/78 (manifest has 78 entries; `loop_tap_counter`, `loop_selfosc`, `loop_hysteresis_bow` are in it) is the stored-patch proof; I did not re-run it.
3. **Tap on a node not pulled this tick.** Pinned: `:2236-2237` (`previous()` after two idle ticks = 4) and `:2296-2297` (`tap->next() == 5` two ticks later). Verified: `claimed_tick()` false → `cur_`.
4. **Tap inside a stepped subgraph.** Pinned for ONE ordering only: `:2292-2295` steps the source twice then the tap (4 = one step late). The other ordering — tap stepped before its source in the same fill — is not pinned and reads two steps late: at step k the source's `lastTick_ == now` from step k-1, so `previous()` returns `prev_` = S(k-2), not S(k-1) (`:86-91` re-claims the same tick, `:104-106` cannot tell a fresh step from an earlier one). Under `next()` the same ordering is order-independent (`:63-70`). Ruling 5's "one step late" is therefore true only for source-before-tap order. Task 0 scan (b): 0 of 62 fills; my tree scan agrees no stored patch has a tap in a fill (taps are only on `CombinedSource.source1/2`, `Wormhole.source`, `CurveNode.source`, `Biquad.source`; 1,452 of 3,144 patches carry taps).
5. **Setup-time read through a tap.** The plan's claim ("the live read must see the value the tap will deliver at the coming tick") is pinned only for a never-pulled tap (`:2289`). For a tap pulled in the tick that just ended — every tap on an audible path, at every note after the first in a pooled voice (`start()` does not reset `lastTick_`, :41-49; the clock stops at the last rendered tick) — `current()` returns the memo (:260), i.e. source(T-1), while the coming tick delivers source(T). That read is asserted at `:2376` as a within-tick fact but its Setup-time meaning is not named anywhere. Not exercised by any stored patch: my scan finds 0 taps on `Envelope.trigger`, 0 in any instrument block (push bindings), 0 on views. See Issue I3.

Also noted (the view note): "a view reached only by taps is on the tail list" (`patch_loader.cpp:163-200`) holds, but a view that is ALSO consumed by a non-pulling Manual pin (`Envelope.trigger`, `envelope.h:46`; read only at `instrument.h:439`) counts as normally consumed (`:186-190`) and is never computed per tick; a tap on it would read a frozen `cur_` under B where A read the live match. 0 stored patches (scan a; my scan agrees). Minor M3.

### Rulings weighed

Planning rulings 1-12 and execution rulings as the report numbers them (13-18).

1. Two values moved at the claim — **stands**; the three-position argument is correct and pinned (:2240-2252). Cost (one float, one store) honest; timing table (not re-run) agrees.
2. `previous()` non-virtual — **stands**, but the tail-list guarantee it leans on has the trigger-pin hole (M3); "no stored patch taps a view" is confirmed by my scan.
3. No clock → `cur_` — **stands**; pinned :2256-2259, :2311-2317.
4. `current()` stays live — **superseded by 18**; as written it was wrong for the delivery path, and the ledger says so. The between-ticks half of it ("the source's present value is what the next tick's tap delivers") is right for a never-pulled tap and wrong for a pulled one (I3a).
5. `step()` moves the value the same way — **mechanism stands, conclusion overstated**: "one step late" is order-dependent (Review Focus 4). Cost-if-wrong honest (no stored patch), but the comment on `step()` (:76-85) should list it as a third limit.
6. Landing order — **stands**; `b0a1a5f` writes `prev_` and nothing reads it (`git show b0a1a5f` confirms `compute()` still reads `source->current()`); 78/78 claimed, not re-run.
7. Measured again with the sealed binary — **stands with a caveat**: the counter sits at the tap's `compute()` position (`a8d5aa9` :241-243) and cannot see a same-tick delivery created by a LATER pin of the same consumer's sweep computing the source before the post-sweep `current()` read. In the sample the A/B found no such patch; outside the sample nothing was looked at (I2). The factor-of-two explanation holds: under 2.3 the tap's `compute()` ran once per tick while the old trace counted `RefSource::read()` twice (sweep + post-sweep `current()`); every shared entry is exactly half, which a position error would not produce.
8. Old arms re-used — **stands**; the diff-stat claim is plausible (docs-only commits between `fdf7536` and `fc7ad11`); not re-verified beyond the ledger.
9. No manifest refreeze — **stands**; none of the 45 is under `library/` or `baselines/` (hash arms: library 0/80, baselines 0/52 differ, verified from disk).
10. `comp = 1` keeps — **the code decision stands, the stated reason is wrong**: the closing taps of every compensated loop on the list are mid-compute under both readings (Issue C1), so `comp = 1` was right before and is right now; the sentence "was false under reading A for every patch on the list" conflates the control-side tap that actually moved with the closing tap. And `comp = 1` is "true in every rooting" only for a cycle with one tap: a second tap on the same cycle is walked through (`delay_line_source.h:112-115`) and now costs one deterministic sample that `comp` does not subtract (M2).
11. Documents record the delta — **stands**; `feedback_loop_design.md:13-16` is honest and §2's "positional" paragraph is correctly dated by the header. Part 2 §7 row 2.4's "whole-tree" is not (I1).
12. Retired pair in the A/B — **stands**; 0 of 2 differ, verified from disk.
13. Gates at step 0 not re-run — **stands**; low cost, honest.
14. Two dropped patches not a counter defect — **stands**; the wiring explanation is consistent with the memo, and both are identical in the A/B (verified: the 7 identical in `taps` are exactly the 5 flat-knot + the 2 dropped).
15. 2.3's live test asserted reading A — **stands**; the moved assertion (:2371) is the position this checkpoint changes.
16. Three failures not one — **stands**; harness behaviour.
17. `-w` is the honest grep — **wrong as scoped**: the check in the plan is over `engine tools`, and `tools/engine_tests/main.cpp:200,321` still say "positional" (M1).
18. `current()` = memo once claimed, live before — **the right call for this checkpoint, with its semantics misstated**. It is the only way to deliver B under the read-after-sweep rule without touching every consumer; the alternatives listed in the report are correctly rejected. But its cost-if-wrong ("a Setup-time reader of a tap pulled earlier in the same tick would see the memo") is not a hypothetical: between ticks every tap pulled in the ended tick IS in that state (I3a), and the stepped-chain path is two-old (I3b). "No stored patch has a tap on a Manual or trigger pin (scans a-c: 0)" is true but scans a-c do not look at trigger pins; my scan does and confirms 0.

### Strengths

- The core mechanism is minimal and right: one store at the claim (:70, :88), one non-virtual read (:104-106), three read positions, one answer — pinned by a test that reads from inside the source's compute (:2240-2252), which is the hard case.
- Reading B is pinned through a real consumer's post-sweep read (:2262-2274), not just the tap's own `next()`; the first byte-identical A/B arm was caught and the test was restated before the code moved.
- The A/B claims verify from disk exactly: 40 distinct patches differ, all 40 on the list, none off it, the five identical ones have flat `NL_curve` knots (`[[-2,1],[0,1],[2,1]]`; the moved `_st1` sibling has `1.06/0.94`), library and baselines 0.
- The measurement code lived one commit and is reproducible from history; the factor-of-two reconciliation with the 10-18 list is exact on all 45 entries.
- The design documents record the delta without rewriting history.

### Issues

#### Critical (Must Fix)

**C1. The report's mechanism for the render change is wrong for 37 of the 40 patches; the renders are being accepted on a false description.**
Evidence: `trombone_attempt4.json` graph — `Bore` (DelayLine, `compensate: true`) is tapped by `Dp`, `Pe`, `Pplus`, and `Force` taps `Pe`; all four sit inside `Bore`'s own pull (output `NL_out` ← … ← `NL_delay` ← `NL_in` ← `Bore` ← `Refl2` ← … ← `LoopIn` ← `Pplus`/`Pe`/`Dp`), so every closing-tap read is mid-compute, where A and B agree (Part 2 §2; pinned :2302-2309). The one same-tick read per sample (648,000 = 13.5 s × 48 kHz for `trombone_attempt1`) is `NL_curve ← tap(NL_in)` feeding `NL_delay.ratio`: `Processor::input` is registered before `ratio` (`delay_line_source.h:16-19`, base subobject first), so `NL_delay`'s sweep computes `NL_in` through `source` before `ratio` reaches the tap. The same block, verbatim, is in `pending/brass/Trombone_att1`, all `sweep/trombone1-4`, `onemass_lip1`, `nlbore_probe1`, `old/nlbore_brass1`. In `pierce1d_1` the same-tick read is `SignCurve ← tap(Delay_line)` feeding `Damp_lpf.cutoffFreq` (pulled after `Damp_lpf.source` computed `Delay_line`), and `compensate: true` there is inert: no tap targets `Delay_line`, so `walk_to_tap` (`:109-131`) never returns true and `cycleFound_` stays false. Only `stk_blowhole_port` (3) changes a signal path: `D1In ← tap(D2)` between two tail DelayLines (`compensate: false`), advanced in file order D2 then D1.
Failure: §1 ("the brass and bore loops whose output is taken downstream of the tap's source"), §5 ("the loop gains the one sample… those loops ran one sample short of the length the setting computes and now land on it"), the §5 family table's `compensate` column, ruling 10's second sentence, and the measurement's scan (d) all describe a loop-period change that did not happen. The actual mechanism: a nonlinearity's control read (bore ratio modulation; damping cutoff) now lags its input by one sample; loop periods are unchanged; `compensate` is irrelevant to the change. REVIEW R5 item 0 asks Matt to accept the frozen renders on the wrong story. (The ledger's own flat-knot explanation for the five identical patches already says the same-tick tap feeds `NL_curve` — it contradicts §5 two paragraphs apart.)
Fix: rewrite §1's parenthetical, §5's mechanism paragraph and "mechanism in numbers", drop the `compensate` column or relabel it "irrelevant", correct ruling 10's reason, and correct scan (d)'s last sentence in the measurement document. The measured numbers (`trombone_attempt4` peak/rms) stand.

#### Important (Should Fix)

**I1. "Whole-tree" / "tree-wide" overstates the coverage, and the structural class on disk is roughly 2.5× the list.**
Evidence: on disk `patches/sweep` has 1,370 patches and `patches/old` 820; the set files carry 457 and 275 (every third; plan ruling 8, Appendix A rows). Patches with `tap(NL_in)` on disk: 101 (sweep/trombone1-4 8+9+10+8, nlbore_probe1 20, onemass_lip1 20, old/nlbore_brass1 20, audition 5, pending 1) plus pierce1d_1 6 and stk_blowhole_port 9 — at least 116 patches of the measured class, against 45 listed and 40 confirmed. Separately, `audition/articulation1/trombone_tongue.json` carries the identical NL block with non-flat knots (1.24/1.0/0.76) but counted 0 and rendered identical, because it is an instrument patch with `"score": null` and the CLI's `run_patch` (`main.cpp:752-780`) renders it through the mixer with no notes — the A/B is vacuous for it, and it is in Matt's listening queue.
Failure: §1 ("Renders changed in one measured class and nowhere else… 40 patches"), §7 row 2.4 ("40 differ in the whole-tree A/B"), Appendix A's title, and the ledger's "40 distinct patches tree-wide" read as a complete inventory. They are the sample's.
Fix: say "of the 1,615-patch sample (sweep and old every third)" wherever "whole-tree"/"tree-wide" appears; name the structural class and its on-disk count (the NL bore block, the pierce damping curve, the blowhole D2→D1 tap) so Matt knows which un-sampled sweeps and which queue patches (`trombone_tongue`) will sound different when played.

**I2. The counter measures the tap's compute position, not the delivery position; the A/B is the actual proof, and only within the sample.**
Evidence: `a8d5aa9` `dsp_value_source.h:241-243` counts in `TapSource::compute()`; the consumer's delivered value under 2.3 was the live `current()` read after the whole sweep (`combined_source.h:45-46`). A consumer whose later pin's cone computes the tap's source after the tap was pulled delivers same-tick under A and counts 0. My scan finds no node with `tap(X)` and `ref(X)` on the same node, but the general shape (a later pin reaching X through other nodes) was not scanned.
Failure: the measurement document presents the list as the set of patches that can change; it is the set the counter can see. Within the sample the A/B shows no straggler, which is the real evidence.
Fix: one sentence in the measurement document and §5: the list is the counter's; the A/B over the same sample is the proof of "none off it".

**I3. The Setup-time semantics are not what rulings 4/18 and §2/§3 say, and none of the three cases is pinned.**
(a) Between ticks, a tap pulled in the tick that just ended has `claimed_tick()` true (`:140`: the clock holds at the last rendered tick; `start()` :41-49 does not reset `lastTick_`), so a Setup-time `current()` read (`instrument.h:439`, `:369`, `:521`) returns the memo, source(T-1), while the coming tick delivers source(T) (`:104-106` → `cur_`, the source not yet claimed). §2 "Before it is pulled — a Setup-time read between ticks — it answers live" and §3 "Setup-time readers unchanged" are false for every tap on an audible path at every note after the first in a pooled voice.
(b) A push chain stepped at Setup (`instrument.h:368`, `:520`) through a tap: `step()` :86-91 → `compute()` :253 → `source->previous()` → the source claimed T → `prev_` = source(T-1); then `chain->current()` reads the tap's `current()` → claimed → that memo. The report rejects "current() always previous()" because "a Setup-time reader would get a value two ticks old" — that is exactly what the `step()` path does.
(c) Inside a fill, the tap-before-source ordering reads two steps late (Review Focus 4).
Failure: the rule "a tap is one sample late wherever it sits" has three documented-as-true positions where it is two; no stored patch exercises them (my scan: 0 taps on trigger pins, 0 in instrument blocks, 0 in fills per scan b), so nothing renders wrong today, but the contract is misstated in the code comment (:254-258), the report (§2, §3, §6 rulings 4 and 18) and the plan's Review Focus 5.
Fix for the seal: state the actual semantics in `TapSource`'s comment and in §2/§3/§7 (debt: "a Setup-time read or a Setup-time `step()` through a tap already pulled this tick serves the memo, one sample older than the coming tick's delivery; the fix is Part 3's `pin.value(t)`, which retires the live `current()` on the tap, or a step generation on the clock"); add two assertions — a tap pulled then read between ticks returns the memo (name it as the Setup case), and a stepped chain through a pulled tap returns source(T-1). A code change is not required for the seal and should not be improvised.

#### Minor (Nice to Have)

**M1. Stale "positional" comments in the tests.** `tools/engine_tests/main.cpp:200-203` and `:320-321` describe the delay as positional; the plan's Task 3 step 1 check (`git grep -n positional -- engine tools`) lists them. Fix: two comment edits (`:2311`'s "no clock: positional" is accurate and stays).

**M2. `comp = 1` counts one closing tap.** `delay_line_source.h:66-70` subtracts one sample for the closing tap; a second tap on the same cycle is walked through (`:112-115`) and under B is one more deterministic sample `comp` does not see (under A it was 0 or 1 by position). No stored compensated patch has a two-tap cycle (blowhole's is uncompensated). Fix: one clause in the `compensate` comment (`:21-27`): "one tap on the cycle; a second tap is one more sample not subtracted".

**M3. The tail-list guarantee has a hole for views.** `collect_advance_ids` (`patch_loader.cpp:186-190`) counts an `Envelope.trigger` edge as a normal consumer, so a view tapped elsewhere and wired to a trigger is neither a tail nor pulled per tick; under B its tap reads a frozen `cur_` (A read the live match). 0 stored patches. Fix: a sentence in §7's view bullet; a load-time warning when a tap targets a node no per-tick pull reaches would be the real fix, later.

**M4. `operator=` copies `prev_` (:161) — correct, inert, untested.** `Envelope::replace_stages` (`envelope.h:193-212`) assigns from a fresh envelope whose `cur_` and `prev_` are 0 and does not copy `lastTick_`; the next claim overwrites `prev_` before any tap can read it. Nothing to do beyond noting it is dead in practice.

**M5. Dates disagree.** The measurement header says "Taken 2026-10-19" (commit `a8d5aa9` is dated Oct 19 16:53 -1000); the file name, Part 2 §2's decision line, the plan and the report say 2026-10-20. Pick one.

**M6. Unexplained scratch artifact.** `renders/scratch/ab24_tree/dropped.txt` lists the two dropped bowed patches and `sweep/trombone1/loud.json` (which differs and is on the list). Not cited anywhere; harmless unless something read it.

### Declined to judge

- The timing table and the gate outputs (ctest, stamp, null gate 78/78, wiring null gate): not re-run, per the no-build/no-render instruction; taken as the ledger reports them.
- The headless UI checks (`--gencheck`, `--gatecheck`): not re-run.
- Whether reading B sounds better on the brass and bore patches: Matt's ears; the renders are derived data and the mechanism (C1, corrected) is the only thing the report owes him.
- The 157 non-loading patches: a 2.2 matter, unchanged here.
- Mixer-mode and drum pull loops (`instrument.h:757-785`): same clock shape, same semantics; not traced separately.

### Assessment

**Ready to seal: yes, with fixes — documentation and tests only; no engine code needs to change.** The mechanism (`prev_` at the claim, `previous()`, the tap's compute) is correct, minimal and pinned; the A/B evidence verifies from disk exactly as claimed. What must change first:

1. C1 — rewrite the mechanism in §1, §5 (paragraph, table, "mechanism in numbers"), ruling 10's reason, and the measurement's scan (d): a control-side modulation read lags one sample; loop periods are unchanged; `compensate` is irrelevant (inert in pierce). Matt's acceptance of the renders must rest on this.
2. I1 — replace "whole-tree"/"tree-wide" with the sample's extent and name the structural class on disk (≥116 patches; `trombone_tongue` in the queue).
3. I3 — state the Setup-time and `step()` semantics as they are in `TapSource`'s comment, §2, §3, §6 (rulings 4, 18) and §7, and pin the two cases in `run_tap_reading_tests`.
4. I2, M1 — one sentence each; the two test comments.

### What was done

The refutation came back with one Critical, three Important and six Minor
findings, and the assessment "ready to seal, with fixes — documentation and
tests only". Every finding was verified before it was acted on (the per-family
tap wiring, pierce's loop walk, the on-disk counts and `trombone_tongue`'s
missing score were re-derived from the JSON with a scratch script), then one
fix wave landed: C1 (the mechanism rewritten in sections 1 and 5, ruling 10 and
the measurement's scan d), I1 (the sample's extent and the class's on-disk
count), I2 (the counter's position), I3 (`TapSource`'s comment, sections 2, 3,
6 and 7, and nine pinning assertions in `run_tap_reading_tests` — they pass on
the existing code, because they state limits), M1, M2, M3, M5 (every 10-20
date was wrong; the plan, the measurement, the report and the retired folder
were renamed and every reference corrected) and M6; M4 noted, nothing to do.
No engine behaviour changed. The same reviewer re-reviewed the fix wave and
asked for three more text edits (the C1 leftover in section 2's `DelayLineSource`
entry, the class count — 116, with `pierce1d_1` at six — and Lesson 2's
"stateless view"), which landed with the nice-to-haves it listed.

# Refutation R5, re-review of the fix wave (the working tree on 557fc01)

Read-only, as before: `git diff 557fc01 -- engine tools docs`, the renamed report, measurement, plan and ledger, and the JSON on disk. Nothing built or run. Line numbers are the working tree.

## Finding Verdicts

| Finding | Verdict | Evidence |
|---|---|---|
| C1 mechanism | FIXED, one leftover | Rewritten correctly in report §1:34-55, §5:160-188 (the three shapes; `Processor::input` before `ratio`; `Bore`'s closing taps mid-compute; pierce's walk finds no closing tap; blowhole D2 before D1 in file order), the family table §5:192-201, §6 ruling 10:313-322, measurement `:389-395`, plan Ledger `:377`. **Leftover:** §2's `DelayLineSource` entry (report :119-124) still says "the assumption was true on every loop skeleton and false on the measured list, and is true everywhere now" — the sentence C1 refuted, contradicting §1:34-38 and ruling 10 two pages later. One sentence to replace. |
| I1 coverage | FIXED, one count wrong | "Whole-tree"/"tree-wide" gone from §1:40, §5:162, Appendix A title :487 and :506-508, measurement :398-399; the class's extent and `trombone_tongue` named at §1:52-55, §5:204-211. **Count:** "120 patches … 10 in `pierce1d_1`" (§5:204-207, §1:52, measurement :401, Ledger :377) is wrong by four: `pierce1d_1` has 6 patches on disk (all six listed and differing); the other four are `pierce1d_2`, whose only tap is `DIn ← tap(Pierce)` — mid-compute, no `SignCurve`, no same-tick read (`grep SignCurve pierce1d_2/*.json` is empty) — and all four were in the audition sample and identical. The class on disk is 116 (101 + 6 + 9). STATUS.md was not given I1 content, only dates and paths (its diff is three hunks), despite the ledger's "Final: fixed I1 … STATUS"; the seal commit's handoff can carry it. |
| I2 counter position | FIXED | §5:213-219; measurement :396-399. Accurate: the counter's position, the A/B over the same sample as the proof. |
| I3 Setup / step semantics | FIXED | `dsp_value_source.h:254-261` states the three cases correctly; report §2:97-102, §3:142, §6 ruling 4:286-288 and ruling 18:362-368, §7:375-384 (the fix routed to Part 3's `pin.value(t)` or a step generation, not improvised). Pinned: `main.cpp:2300-2317`, traced below. No engine behaviour changed (the diff touches only comments in `engine/`). |
| M1 stale test comments | FIXED | `main.cpp:199-204` (now: no-clock positional, the rule under a clock) and `:322-324` ("the closing tap's one sample"). `grep positional tools/engine_tests/main.cpp` leaves only `:2331`, which is accurate (no clock). |
| M2 compensate counts one tap | FIXED | `delay_line_source.h:26-29`; report §7:388-391; ruling 10:320-322. |
| M3 view-on-trigger tail hole | FIXED | Report §7:394-400 (the hole named, the load-time warning as the later fix). Ruling 2 (:274-278) still states the guarantee unqualified; §7 covers it — nice to add "(section 7)" there. |
| M4 `operator=` copies `prev_` | RULED-DEFERRED | Ledger "Final: minor (deferred): M4"; correct — inert, nothing to do. |
| M5 dates | FIXED | Plan, measurement, report, `.superpowers/sdd` folder and `patches/old/baselines_retired_2026-10-19/` renamed; `grep -rn 2026-10-20 docs engine tools .superpowers` (GOALS.md excluded) is empty. See the out-of-scope note on the gitignored set files. |
| M6 dropped.txt | FIXED | `renders/scratch/ab24_tree/dropped.txt` is gone. |

## New Breakage in the Fix Diff

**The three new pinned cases, traced by hand** (`main.cpp:2297-2317`; state entering the block from the existing `:2296-2298`: `c` lastTick 4, prev 4, cur 5; `tap` lastTick 6, prev 4, cur 5; now = 6):

- tick → 7. `c->next()`: claims 7, prev 5, cur 6 → **6** ✓. `tap->next()`: claims 7, prev 5, cur = `c->previous()` → c claimed 7 → c.prev = **5** ✓. `tap->current()`: tap claimed 7 → cur = **5** ✓ (the Setup-case memo; the coming tick would deliver 6). `tap->step()`: lastTick 7, prev 5, cur = `c->previous()` = **5** ✓.
- tick → 8. `tap->step()`: c lastTick 7 ≠ 8 → c.cur = **6** ✓. `c->step()`: claims 8, prev 6, cur **7** ✓. `tap->step()`: c claimed 8 → c.prev = **6** ✓ (one-late would be 7: two late, as the comment says). `c->step()`: prev 7, cur **8** ✓. `tap->step()`: c.prev = **7** ✓ ("7, not 8").

All nine values hold against `next()` :63-75, `step()` :86-92, `previous()` :104-106, `claimed_tick()` :140 and `TapSource::current()` :259-261. The block sits before the fresh `counter`/`self`/`bare` objects (:2318 on), so the later assertions are unaffected by the extra ticks. The block's two comments are accurate.

**Comments' accuracy:** `TapSource` :254-261 — correct in all three cases (never-pulled live; pulled-in-ended-tick memo; `step()` through it the same memo) and the Part 3 pointer. `delay_line_source.h:21-29` — correct. `main.cpp:199-204`, `:322-324` — correct. Report §5's wiring statements checked against the JSON: `NL_delay` pin order, `Bore`'s three taps inside its own pull, `Damp_lpf` as the tail whose `source` computes `Delay_line` before `cutoffFreq` reaches `SignCurve`, blowhole's node order D2 before D1 — all true.

**Renamed paths:** every tracked reference follows the renames (grep above; plan :8, :48-50, :188, :194, :201-202, :331, :339; Part 2 §2 and §7; STATUS, both BACKLOGs, REVIEW, the 2.3 report). Nothing in `engine/` or `tools/` names the dates.

**Two new inaccuracies introduced or left by the rewrite, besides the C1 leftover and the count:**
- Lesson 2 (report :451-462) still calls `TapSource` one of "three stateless views" whose `current()` "is computed on the spot, because the view holds no state of its own". Under this checkpoint the tap holds a memo and serves it once it has claimed the tick (:259-261); only the never-pulled read is live. One clause ("the tap, until it has claimed the tick") keeps the lesson honest.
- The Ledger's correction line (plan :377) says "ruling 8's 'one more sample of loop latency'" — that sentence is planning ruling 10's and Task 0 step 3(d)'s, not ruling 8's (old arms re-used). A cross-reference slip.
- Ruling 5 (:289-292) still says "one step late" without the ordering caveat that §7:379-381 and ruling 18 now state; a pointer would do.

## Out-of-Scope Observations

- The gitignored A/B inputs still carry the old folder: `renders/scratch/ab24_tree/old.txt:274-275`, `retired.txt:1-2`, `retired/old/hashes.txt`, `old/new/hashes.txt:28-29` name `patches/old/baselines_retired_2026-10-20/…`, which no longer exists on disk. Appendix A's "the set files are `renders/scratch/ab24_tree/*.txt`" is therefore not re-runnable for those two patches without editing the two set files; the recorded hashes are unaffected.
- `docs/autonomy/GOALS.md` carries an unrelated 121-line working-tree change that predates the review; a `git add -u` / `commit -a` for the seal would sweep it in.
- `tools/engine_tests/main.cpp` and the headers show "LF will be replaced by CRLF" warnings on `git diff` — pre-existing autocrlf state, not introduced here.

## Verdict

The fix wave addresses every finding: the mechanism is now stated correctly where Matt will read it (§1, §5, ruling 10, the measurement), the coverage is honest, the Setup-time and `step()` semantics are stated in the code and pinned by nine assertions that trace correctly, and no engine behaviour moved. Before the seal, three small text edits must still land: replace the `DelayLineSource` sentence in report §2:122-124 (the C1 claim survives there); correct "120 … 10 in `pierce1d_1`" to 116 and 6 in §1:52, §5:204-207, the measurement :401 and the Ledger :377 (pierce1d_2's four are not in the class and were in the sample, identical); and qualify Lesson 2's "stateless view" for the tap. The ruling 5/ruling 2 pointers and the Ledger's "ruling 8" slip are nice-to-have. With those, ready to seal.

## 9. Reading guide

Ten minutes: `dsp_value_source.h` `next()`, `step()`, `previous()` and
`TapSource` whole; then `run_previous_tests` and `run_tap_reading_tests` in
`tools/engine_tests/main.cpp` (every read position, with the values worked
out), and the measurement document's "Against the 2026-10-18 list" section for
why a count halves and why two patches left the list.

## 10. C++ lessons

### Lesson 1: the unit delay as state

In DSP notation z⁻¹ is "the previous value", and every delay line, filter and
feedback loop is built from it. The cheapest delay line there is holds one
number: the value a node had before it computed the current one. That is what
`prev_` is — a one-sample delay line inside every node, written in one place
(at the claim of a tick) and read by one kind of consumer (the tap edge).
Before this checkpoint the delay was not stored anywhere; it was an accident of
evaluation order, present when the tap happened to be evaluated before its
source and absent otherwise, and the design document's sentence "a tap reads
what its source computed last tick" was true only on the loop skeletons it was
written for.

The C# reader has met the shape as a `Previous` property on a model whose
`Current` is updated by one method — `Value`/`PreviousValue` on a view-model,
or a double-buffered frame in a game loop, where the buffer you read is the
one you did not just write. The rule that makes it correct is the same in
both languages: the move from current to previous happens in exactly one
place, at a well-defined moment, and nobody else writes either field. Here the
moment is "the tick is claimed", chosen so that three different readers (before
the claim, mid-compute, after the store) agree; lesson 2 of report R4 (an
observing pointer in a value-type context) gave the clock that defines the
moment.

### Lesson 2: a read is not a pull

`previous()` is `const`, non-virtual and has no side effect. `current()` on the
same class is virtual, and three views override it to answer live — two of
them stateless, and the tap only until it has claimed the tick, after which
it serves its memo (section 2).
Why the difference? `current()` answers "what is this node's value right now",
and for a view (a `NameGate` matching a name, a `PerformOut` reporting the
performer's frequency, a `TapSource` reading another node) the honest answer
is computed on the spot, because the view holds no state of its own — a
Setup-time caller between ticks needs the note just delivered, not the memo
of the tick before. `previous()` answers "what did this node's memo hold at
the end of the previous tick", which is a fact about the base's two fields;
no subclass has a better answer, so no subclass may override it. Making it
virtual would invite a view to forward it live, which is exactly the wrong
value for a tap.

The Java/C# analogue: a `final` (C#: non-virtual) method beside a virtual
property on the same abstract base. The decision rule is the one used
throughout Part 2 — a method is virtual when a subclass has a legitimately
different answer, and sealed when the base owns the fact. `next()` is sealed
because the base owns the memo; `compute()` is pure virtual because the node
owns the work; `current()` is virtual because a view owns its present;
`previous()` is sealed because the base owns the past.

### Lesson 3 (short): measuring with code that must not survive

The expected-diff list was measured by code that lived for one commit: a
`static inline` counter on `TapSource` (C++17 lets a header define a static
member in place), one line in `compute()`, and a printer at the top of
`main()` written as a local struct with a destructor, so it prints on every
return path including the `catch`. All of it breaks rules the engine keeps —
a file-level mutable, a global read on the sample path — which is why none of
it may stay, and why it was committed before it was deleted: `git show
a8d5aa9:tools/measure_taps.py` and the header at that commit rebuild the
measurement exactly. The C# reader would reach for a conditional-compilation
symbol (`#if MEASURE`) and leave it in; the cost of that habit is a second
engine inside the first, never built, slowly going wrong. A measurement is a
commit, not a mode.

## Appendix A: the A/B over the sample

The pre-2.4 binary (`renders/scratch/ab_tree/cli_pre24.exe`, the engine at
`fdf7536`; the six tree sets' old arms are 2.3's `final` arms, re-used)
against the Task 2 binary (`cli_t2.exe`, `557fc01`), every patch rendered by
both, WAV bytes compared. The set files are `renders/scratch/ab24_tree/*.txt`,
the hashes under `renders/scratch/ab24_tree/<set>/{old,new}/hashes.txt`.

| set | patches in the set | rendered by both | differ |
|---|---|---|---|
| `taps` (the 47 of the 10-18 list, in full) | 47 | 47 | 40 |
| `retired` (the `loop_root_sensitivity` pair) | 2 | 2 | 0 |
| patches/library | 80 | 80 | 0 |
| patches/baselines | 52 | 52 | 0 |
| patches/pending | 43 | 40 | 1 |
| patches/audition | 708 | 708 | 10 |
| patches/sweep, every third | 457 | 419 | 24 |
| patches/old, every third, plus the retired pair | 275 | 157 | 5 |

Forty distinct patches differ across the sets (the `taps` set overlaps the
tree sets) — of the 1,615-patch sample, not of the tree: `sweep` and `old` are
every third patch, and section 5 gives the class's extent on disk. Every one
is on the measured list and none is off it; the five
listed patches that did not move are named in section 5. A patch missing from
"rendered by both" failed to render in both binaries (the same 157 that do
not load since checkpoint 2.2). The `baselines` compare reports the retired
pair as present in one arm only (its old arm was rendered at the pair's
former path); `retired` covers it. The first arm rendered under the plan's
original `TapSource::current()` (section 6, ruling 18) was byte-identical on
all 47 tap patches and was discarded; the arm reported here is `557fc01`'s
engine (`cli_t2.exe`).
