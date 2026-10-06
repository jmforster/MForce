# Refactor lane — BACKLOG

The campaigns of the 2026-10-02 review, in the order of surgery decided
in `docs/architecture/target-architecture.md` section 8. Each part's
checkpoints are listed in that part's document; this file tracks which
have landed.

| Step | What | Spec | Status |
|---|---|---|---|
| 0 | Meter, CTest registration, one gate command | gates spec §0 | DONE 10-05 (`637207a`), report R1 |
| 1 | Part 2: the ValueSource contract (checkpoints 2.1–2.6) | `docs/architecture/part2-valuesource-contract.md` | signed; plan next |
| 2 | Part 3: nodes (checkpoints 3.1–3.8) | `docs/architecture/part3-nodes.md` | decided; plan next |
| 3 | Part 4: patch | not yet written (max) | — |
| 4 | Part 5: render, contract, the one performer, play | not yet written (max) | — |
| 5 | Part 7: UI | not yet written (xhigh) | — |
| 6 | Part 6: music | not yet written (xhigh) | — |
| 7 | Part 8: build, tests, shelved target; then phase 2 gates on | not yet written (xhigh) | — |

Standing: no feature work in any lane until Matt lifts the freeze
(CLAUDE.md, "Scope right now").
