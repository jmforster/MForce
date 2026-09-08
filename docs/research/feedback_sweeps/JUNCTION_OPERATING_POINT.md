# Junction operating point — mechanism session (2026-09-04)

Interactive session, Matt + Fable 5. Started as "explain the junction curve,
it seems a bit magical"; ended with an ear-validated recipe and a reframing
of the r5c asymmetry axis. Patch artifact: **patches/pending/oboes/
oboe_dv_junc_tweaked.json** (Matt's sandbox — NOT a queue item; stays there
until he names it. Nothing supersedes oboe_default; "all good in different
ways").

## Mechanism (grounded in oboe_default numbers)

- Junction computes `y = curve(drive · x)`. The circulating signal only sees
  the window of the curve its excursions cover; local slope = gain.
  Equilibrium amplitude lands where average round-trip gain = 1 — level
  regulator and tone generator are the same object.
- Effective small-signal loop gain = drive × slope-at-origin × loop losses
  (damp filter). Growth is exponential per round trip (one trip per period),
  so bloom time goes with log(gain): small slope/drive changes swing the
  breathy window from ~1 s to ~ms.
- The breathy attack is not an ingredient — it's the audible window where
  breath noise ≥ the not-yet-grown tone. Once locked, the loop actively
  squelches additive noise (knee compression = perturbation suppression),
  so a hot loop kills breath twice over.

## Measured: one pass vs settled (renders/library/oboe_default.wav, C3)

| Harmonic | one pass, sine, drive 0.63 | settled render |
|---|---|---|
| H2 | −31 dB | −30 dB |
| H3 | −24 dB | −8.5 dB |
| H5 | −27 dB | −13 dB |
| H7 | −28 dB | −21 dB |

Odd harmonics gain ~15+ dB from recirculation. **Evens don't grow** — the
curve is nearly odd-symmetric inside the moderate-drive window; its
asymmetry lives at the outer clamps (−1.0 hard vs +0.93 soft), which the
signal only reaches at high drive. Consequence: drive controls even/odd
balance, not just brightness.

## Ear-validated recipe (Matt, same day)

1. **Asymmetry at the origin, not (only) at the clamps.** Matt dragged the
   point left of origin to (−0.081, −0.256): down-swing slope 2.1 → 3.16 vs
   up-swing 1.68. Evens at all drive levels → "intense character of the live
   oboe pops out."
2. **Drive floor below oscillation threshold.** Steepening the slope made
   the old floor hot: Drive_env minValue 0.533 × 3.16 ≈ 1.7 gain at zero —
   whole envelope range = "on, hard," breath vanished, and stage-value edits
   did nothing (see trap below). Threshold ≈ 1/slope + filter losses ≈
   0.35–0.40. **minValue 0.3 landed it**: sub-threshold start = breath-only
   window, ignition where the expo stage crosses threshold. Matt: "sounds
   pretty much like the [YouTube reference] dude."
3. Net: character (curve shape / maxValue) and articulation (minValue /
   attack-stage shape) sit on separate knobs.

## Trap for future tuning

Envelope stage values are 0..1 normalized and map through the envelope's
own minValue/maxValue. Editing stage vals inside a narrow min/max band
(0.533–0.730 here) is inaudible when the whole band is past threshold.
The ignition knob is **minValue**, not the stage shape. (Possible UI fix
discussed: label envelope preview y-axis min→max instead of 0..1.)

## r5c reframing

Replace blind ±15% breakpoint jitter with two directed axes:
- origin asymmetry ratio (down-slope / up-slope through zero) — even-harmonic
  content at all drive levels; candidate for the bowed/brassy character axis
  (bow friction is asymmetric around the slip point; cf. oboe_again bass).
- drive floor relative to measured oscillation threshold (per cell:
  threshold ≈ 1/slope₀ adjusted for damp losses) — articulation axis,
  sub-threshold start time as the parameter.
