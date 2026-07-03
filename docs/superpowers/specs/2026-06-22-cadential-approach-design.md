# Cadential Approach — apply_cadence v2 (head-preserve + distance-aware approach + settle)

**Date:** 2026-06-22
**Status:** Approved (design); pre-implementation
**Addresses:** Audition issue #4 — the cadence is a single yanked tonic note with no approach ("tacked-on").
**Relates to:** [[project_cadence_approach]], [[project_pac_held_note]].

## Problem

Today `apply_cadence` ([default_strategies.h:307-331](engine/include/mforce/music/default_strategies.h)) computes where the final figure naturally lands and, if it's off target, shifts **only the last note's step** to the target. No approach gesture, no rhythmic settling — hence the "tacked-on tonic." Matt's weighting: ~65% melodic approach, ~35% rhythmic settling.

## Design — reshape the landing figure (approach B)

Don't introduce a new cadential idea; **keep the figure's head and rewrite its tail** into a real approach. In a Parallel period this gives "begin identically, then deviate at the cadence" *for free*: the antecedent and consequent final figures are the same atom, so preserving the head and rewriting each tail toward its own target (V for the HC, I for the PAC) makes them share an opening and split at the cadence.

### 1. Head / tail split

- **head = max(0, N − 3)** notes preserved; **tail = the last min(3, N) notes** (always a ~3-note approach). (5→keep 2, 6→keep 3, 7→keep 4; N=4→keep 1; N≤3→whole figure is the approach.)
- `headEndDeg` = the scale degree the melody sits on after the preserved head (start degree + connector leadSteps + steps of all prior figures + steps of the head notes).

### 2. Tail melodic approach (the 65%) — distance-aware buckets

Rewrite the tail's **steps** so the melody walks from `headEndDeg` to the target degree `T`, choosing a shape by distance:

- **On target (`headEndDeg == T`):** neighbor-escape — step away (to 2 or 7) and resolve back to T; or, if only one tail note, hold T (repeated tonic).
- **Close (1–2 scale-steps off):** stepwise to T. Penultimate naturally lands on **2** (approach from above) or **7** (from below).
- **Far (≥3 off):** leap the first tail note toward T to land within a step or two, then step in. Penultimate may be **2/7**, or a **5→1** leap when geometry gives it.

**Penultimate 7 / 2 / 5 / repeated-1 are all acceptable — none is forced.** They emerge from the geometry. (Richer idiom-specific shapes are v2; see below.)

### 3. Rhythmic settle (the 35%) — elide within the beat budget

Rewrite the tail's **durations**: use only as many notes as the approach needs, **elide** the remainder, and add their freed duration to the final tonic so it lands long. **Total figure duration is unchanged** — elision redistributes *within* the figure's beat budget, so the bar grid (verified intact across the 20-render sweep) is preserved; a busy figure just collapses into "a few approach notes + a held goal." Default: land the goal on at least the last 1–2 beats of the figure (tunable).

## Tunable defaults

- Head-length rule: `max(0, N − 3)`.
- Final-note minimum duration: ≥ last 1–2 beats of the figure.
- Distance bucket thresholds (close ≤2, far ≥3).

## Deferred to v2

A **library of named cadential formulas** selected by context — descent-with-leading-tone-overshoot (`3-2-1-7-1`), the `5-4-3`-then-leap-to-tonic cliché, HC/PAC idiom-specific gestures, genre variants. v1's distance buckets guarantee *a* musical approach every time; v2 swaps in stronger idiomatic gestures. (Matt: "start simple.")

## Where it lives

Rewrite `DefaultPhraseStrategy::apply_cadence` in `default_strategies.h`. Same call site (composer.h, gated on `cadenceType>0 && cadenceTarget>=0`, last figure not Literal/Locked). The function already locates the cadence-bearing figure and computes degrees; v2 replaces the single-note shift (line 329-331) with the head/tail reshape above.

## Verification

Render the 20-seed period sweep and confirm:
- Every phrase still lands on its target degree (HC→V, PAC→I).
- The **last ~3 notes form an approach** (a stepwise/leap-in gesture), not a single yanked note.
- The **final note is longer** than the eled tail notes (settling).
- In a Parallel period, the antecedent and consequent final figures **share their head** and diverge only in the tail.
- Bars remain whole (figure total durations unchanged; onsets still on the grid).
- Authored periods unaffected: K467 sets `cadenceTarget = -1`, which skips `apply_cadence` at the call site entirely — so its hand-authored cadential figures are untouched.

## Non-goals

- v2 formula library (above).
- HC/PAC idiom differentiation beyond the target degree (both use the same bucket logic, different `T`).
- Harmonic-context-driven cadence (chord-aware) — out of scope; this is melodic.
