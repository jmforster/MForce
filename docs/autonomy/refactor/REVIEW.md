# Refactor lane — REVIEW (Matt's interface)

Format, same as dsp/ and comp/: what to review → what a verdict decides.
Each refactor checkpoint lands here with its architecture report. Decisions
on the shape of the code happen in docs/architecture/ (the target
architecture and the per-area parts); this file is where finished
checkpoints wait for Matt's read.

## Awaiting

**R3 — checkpoint 2.2: prepare(ctx) / start(frames)** (2026-10-18, commits
`7e20e5f`..`5980873`)
- Read: `docs/autonomy/refactor/reports/2026-10-17-checkpoint-2.2.md`
  (its reading guide is ten minutes; section 6 holds the decisions taken
  on your behalf, section 7 the debt, section 8 the refutation and the
  seven per-task reviews, section 10 three C++ lessons).
- Try: `python tools/null_gate_perform_source.py --jobs 3`; open the UI,
  play a key, Generate — every node is now prepared once by the loader
  and only started per note; a patch saved from the UI records the rate
  the audio stream actually opened with.
- A verdict decides: (0) **DelayLineSource's stray read** (section 7,
  first item): a pre-existing one-past-the-end read is kept byte-identical
  through an extra always-zero ring slot; the correct fix (wrap to slot 0)
  changes the bytes of at least one baseline — leave it listed for
  checkpoint 2.5, or take the render change now with that baseline
  refrozen; (1) whether the UI preparing its editor graph again at every
  play (not per note; idempotent) is the behaviour you want, or whether a
  node should be prepared once when it is created; (2) the list of
  `start()` bodies that still allocate (section 7) as the starting list
  for checkpoint 2.6's test; (3) `render_context()` and `prepared()` as
  public read-only getters on every node (section 6, item 13).

## Resolved

**R2 — checkpoint 2.1: pins declared once** (2026-10-17, commits
`1b9b69f`..`5f9aeea` plus the close)
- Read: `docs/autonomy/refactor/reports/2026-10-16-checkpoint-2.1.md`
  (its reading guide is ten minutes; section 6 holds the decisions
  taken on your behalf, section 7 the debt, section 10 the lessons).
- Try: open any patch in the UI — the Properties panel is served from the
  pin registry now; `python tools/null_gate_perform_source.py --jobs 3`.
- A verdict decides: (0) **the render-time cost** — oboe1 renders about
  30-40% slower than before the checkpoint, piano 25-45%, viola 2-9%
  (report section 5: measured, bisected, experimented; a few nanoseconds
  per node per sample that the hand-written next() bodies did not pay).
  Carry it to checkpoint 2.3, where the per-sample path is rewritten
  anyway, or stop and recover it first; (1) whether the two tolerated
  byte-identity classes (section 6, items 6 and 7 — wiring that no stored
  patch has, retired by the memo in 2.3) are acceptable as named, or must
  be closed by Manual pins now; (2) whether the eight stored patches that
  now state the RedNoise defaults they relied on should keep those keys
  (they do today); (3) the descriptor-order changes the UI shows
  (WaveSource's amplitude first; the presets' and Partials subclasses'
  inherited settings first).

**R1 — checkpoint 0: the structure meter** (2026-10-05, commit `637207a`) — VERDICT 2026-10-16: format accepted; R2 (checkpoint 2.1) uses it unchanged.
- Read: `docs/autonomy/refactor/reports/2026-10-05-checkpoint0-meter.md`
  (the first architecture report in the agreed format; ten minutes via
  its reading guide).
- Try: `python tools/structure/check.py` from the repo root, and
  `python tools/gates.py --fast`.
- A verdict decides: whether the report format gives you what you asked
  for (types and files with responsibility, users, dependencies, the
  design choice and its alternative, the name, lines to read; generated
  metrics; decisions; debt; the independent review; a reading guide; a
  C++ lesson), before checkpoint 2.1 produces the next one.

