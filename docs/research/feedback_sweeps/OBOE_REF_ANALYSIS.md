# oboe_default vs real oboe (YouTube ref) — 2026-09-03

Reference: YouTube 8nNilTdpDiE (Ode to Joy on oboe, 20 s), analyzed against
oboe_default rendering the same phrase (Eb, tonic note 75, renders/scratch/
otj_oboe_eb6.wav). Script: research/ml_ears/oboe_ref_compare.py. The player
performs in D (tuned ~35 cents sharp); matched-note comparison used ref
796 Hz vs render 780 Hz, both ~0.57 s sustained.

## What already matches

- **Breath noise level: dead on.** Harmonic-to-interharmonic gap 60.0 dB
  (ref) vs 59.6 dB (render). Matt's hand-tuned breath curve needs nothing.
- Pitch/intonation clean; loop is on pitch through the phrase.

## The structural gap: even harmonics

At the matched note, harmonic amplitudes rel. H1:

| H | ref | render | delta |
|---|-----|--------|-------|
| 2 | **+6.9** | −34.6 | **−41.5** |
| 3 | +3.5 | −17.1 | −20.6 |
| 4 | **+4.6** | −35.9 | **−40.4** |
| 5 | −6.0 | −24.8 | −18.7 |
| 6 | −17.5 | −40.5 | −23.0 |

The real oboe puts its STRONGEST energy in H2–H4 (that's the conical-bore
signature — even harmonics fully present, spectrum peaking in the formant
region, not at the fundamental). The render is fundamental-dominated with a
clear odd-harmonic bias (H3 > H2, H5 > H4, H7 > H6) — the closed-cylinder /
clarinet pattern. Mechanism: the Junction Shaper curve is essentially
antisymmetric (odd function → odd harmonics). A real double-reed table is
one-sided (reed slams shut on one pressure side) — an ASYMMETRIC curve.

**Hint 1 (the big one): break the junction's odd symmetry.** Candidates:
edit the curve so the positive half differs from the negative half (harder
close on one side), or bias the junction's operating point off zero. The
dcblock already guards the DC this creates (r3 lip lesson). This is also a
natural sweep axis: asymmetry amount, 0 → full one-sided.

## Formant centers

LTAS envelope peaks: ref **1494 Hz** and **3006 Hz** (3 kHz is the
*strongest* peak, prominence 42.5); render has its formant bump near
1600 Hz but upper structure sits ~15 dB lower relative, nothing special
at 3 k.

**Hint 2:** retune the formant SVF toward ~1450–1500 Hz, and add the
second bandpass at ~3.0 kHz — with MORE gain than the first (the ref's
3 k peak outranks its 1.5 k one). My earlier 1.1 kHz suggestion measured
low against this player.

## Vibrato

Ref: **6.8 Hz**, modest depth (~6 cents measured on the matched note —
likely an underestimate; longer ref notes visibly swell). Render: none
detected on a 0.57 s note — the Vibrato node's attack (default 0.3 of
note duration) means it barely arrives on short notes.

**Hint 3:** rate → ~6.8 Hz; reduce the Vibrato `attack` so it's audible
within half a second; depth starting point 10–20 cents.

## Attack / envelope

Ref 10→90% rise on the matched (mid-phrase, legato) note: **435 ms** —
the player swells into notes. Render: 23 ms. Noting the tension with the
r5b tour verdict ("attack too long on some"): the reference argues for
slow swells on phrase notes, at least legato ones. Ears rule; the numbers
are just here for the record.

## Round 2 (same day) — after Matt's formant retune

Matt: formants at ~1.5k/3k with resonance 5 and 7, drive attack reduced
to 0.15 (his call: the ref's 435 ms rise was *expression*, not attack —
the ref's true attack on a detached note measures 30 ms; render 39 ms.
Confirmed: longer attacks "ruined the sound").

Re-measured (matched note now the long opener, ref 753 Hz / 1.7 s):
- **H2 deficit −41.5 → −12.9 dB; H4 −40.4 → −14.2.** The formant boost
  recovered most of the even-harmonic energy perceptually; the odd bias
  still shows at H6/H7 (−52.5 vs −44.0) — junction asymmetry remains the
  structural item.
- LTAS: render's 1.6 k peak now matches/exceeds the ref's 1.5 k; the 3 k
  peak is still ~8 dB underweight relative to the 1.5 k one (in the ref,
  3 k is the STRONGER peak) — the remaining formant-balance gap.
- Vibrato now registers: render 6.8 Hz / ~2 cents vs ref 5.9 Hz /
  ~9 cents — rate right, depth has headroom.
- Noise: on this (breathy, long) ref note the render is 21 dB cleaner —
  the opening-note breath surge is an expression feature the patch
  doesn't do; steady-state noise was already matched in round 1.

## Sustain brightness

Ref holds ≥−40 dB out through H8 and stays −45..−57 to H20; render cliffs
after H8 (−70 by H14, deltas −20..−45). The damp keytrack at a=6 kills
recirculation above ~5 kHz at this pitch. The 3 kHz formant (Hint 2) will
recover part of this; raising the cutoff multiple is the other lever
(r5b's m9 cells are exactly this experiment).
