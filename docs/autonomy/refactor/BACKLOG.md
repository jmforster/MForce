# Refactor lane — BACKLOG

The campaigns of the 2026-10-02 review, in the order of surgery decided
in `docs/architecture/target-architecture.md` section 8. Each part's
checkpoints are listed in that part's document; this file tracks which
have landed.

| Step | What | Spec | Status |
|---|---|---|---|
| 0 | Meter, CTest registration, one gate command | gates spec §0 | DONE 10-05 (`637207a`), report R1 |
| 1 | Part 2: the ValueSource contract (checkpoints 2.1–2.6) | `docs/architecture/part2-valuesource-contract.md` | 2.1 DONE 10-17 (`1b9b69f`..`9f2b4a8`), report R2 accepted 10-17 (render cost carried to 2.3); 2.2 DONE 10-18 (`7e20e5f`..`5980873`), report R3 decided 10-18 (item 3 landed as `6a32e55`); 2.3 DONE 10-18 (`22bf3be`..`fdf7536`), report R4 decided 10-19 (the `loop_root_sensitivity` pair retired; two post-refactor revisits → row 9); 2.4 DONE 10-19 (`a8d5aa9`..`403269f`), report R5 awaiting Matt; 2.5 (no throw on a render path; fault flags) next |
| 2 | Part 3: nodes (checkpoints 3.1–3.8) | `docs/architecture/part3-nodes.md` | decided; plan next |
| 3 | Part 4: patch | not yet written (max) | — |
| 4 | Part 5: render, contract, the one performer, play | not yet written (max) | — |
| 5 | Part 7: UI | not yet written (xhigh) | — |
| 6 | Part 6: music | not yet written (xhigh) | — |
| 7 | Part 8: build, tests, shelved target; then phase 2 gates on | not yet written (xhigh) | — |
| 8 | Workaround cleanup, after Part 8: re-ask "are we growing as much compensating code as we lose?" (asked 2026-10-19 at the 2.3 seal: the engine is 2,800 lines smaller than before 2.1, but about ninety guard sites exist that did not — byte-identity preservations, idempotence guards, `step()`; three section-7 lists of 14, 10 and 13 items) and retire what remains: every byte-identity preservation and until-X guard in the reports' section 7s. Renders MAY change there for engine improvement or simplification — byte-identity is the refactor's instrument, the patches are not gold (Matt, 2026-10-19) — except `patches/library/winds/oboe1.json`, which stays byte-identical or Matt re-listens before it moves. | this row | slated |
| 9 | Post-refactor revisits (Matt's R4 verdict, 2026-10-19: "keep for now, revisit after the refactor"). (a) `step()` as a verb on `ValueSource` — one sample now through every Auto pin, whatever the tick; two callers (the WavetableSource and HybridKSSource table fills, which draw N samples in one tick); two stated limits (a Manual pin or an owned sub-source inside the stepped subgraph serves the memo for the rest of the fill; a node on two pins of the subgraph steps once per pin). Part 3's node pass owns the structural fix of the limits (a pull mode the pins honour, or `compute()`-pulls-its-pins per family where a fill becomes a generator call); this row asks afterwards whether the verb survives at all, or the two fills drive a private excitation and `ValueSource` has one pull verb again. (b) `ShaperSource`'s `breakaway` and `capture` are Auto pins, pulled every sample in both modes though `compute()` reads them only with hysteresis on (2.3 rulings 11 and the Task 4 execution ruling: a threshold's modulator switched on mid-note has been running since the note began; 0 of 1,298 Shaper patches differ). Revisit: Manual again outside hysteresis mode, or Auto stands as the rule for every pin a mode can switch on. | 2.3 report §6 items 3 and 11, §7 | slated |

Standing: no feature work in any lane until Matt lifts the freeze
(CLAUDE.md, "Scope right now").
