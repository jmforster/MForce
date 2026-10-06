# Refactor lane — REVIEW (Matt's interface)

Format, same as dsp/ and comp/: what to review → what a verdict decides.
Each refactor checkpoint lands here with its architecture report. Decisions
on the shape of the code happen in docs/architecture/ (the target
architecture and the per-area parts); this file is where finished
checkpoints wait for Matt's read.

## Awaiting

**R1 — checkpoint 0: the structure meter** (2026-10-05, commit `637207a`)
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

## Resolved

(none yet)
