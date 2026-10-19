# Refactor lane — BACKLOG

The campaigns of the 2026-10-02 review, in the order of surgery decided
in `docs/architecture/target-architecture.md` section 8. Each part's
checkpoints are listed in that part's document; this file tracks which
have landed.

| Step | What | Spec | Status |
|---|---|---|---|
| 0 | Meter, CTest registration, one gate command | gates spec §0 | DONE 10-05 (`637207a`), report R1 |
| 1 | Part 2: the ValueSource contract (checkpoints 2.1–2.6) | `docs/architecture/part2-valuesource-contract.md` | 2.1 DONE 10-17 (`1b9b69f`..`9f2b4a8`), report R2 accepted 10-17 (render cost carried to 2.3); 2.2 DONE 10-18 (`7e20e5f`..`5980873`), report R3 decided 10-18 (item 3 landed as `6a32e55`); 2.3 (the per-tick memo) PLANNED 10-18 (`docs/superpowers/plans/2026-10-18-part2-checkpoint-2.3.md`), awaiting Matt's read |
| 2 | Part 3: nodes (checkpoints 3.1–3.8) | `docs/architecture/part3-nodes.md` | decided; plan next |
| 3 | Part 4: patch | not yet written (max) | — |
| 4 | Part 5: render, contract, the one performer, play | not yet written (max) | — |
| 5 | Part 7: UI | not yet written (xhigh) | — |
| 6 | Part 6: music | not yet written (xhigh) | — |
| 7 | Part 8: build, tests, shelved target; then phase 2 gates on | not yet written (xhigh) | — |

Standing: no feature work in any lane until Matt lifts the freeze
(CLAUDE.md, "Scope right now").
