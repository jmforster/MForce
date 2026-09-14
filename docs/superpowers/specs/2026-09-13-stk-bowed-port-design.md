# STK Bowed port — design

2026-09-13, steal-first campaign (steering meeting decision: start from
proven code, verify against its own output, then extend with MForce-native
features).

## Goal

Replicate STK's `Bowed` instrument (Smith 1986 / MSW 1983 waveguide bowed
string; Cook & Scavone, Maestre body 2011) as an MForce patch, verified
against ground-truth WAVs rendered by the actual STK code. Success is
measured agreement with the reference, not ears.

## Reference

- Code: `C:/@dev/repos/stk` (sibling checkout).
- Driver: `tools/stk_ref/bowed_ref.cpp` — minimal compile (7 STK sources,
  no RtAudio), 48 kHz, deterministic. 10 feature variants x 5 notes
  (C3..C7, 2 s each, bow lift at 1.7 s), mono 16-bit WAVs in
  `renders/scratch/stk_ref/bowed/`.
- Measured reference behavior (autocorr f0, cents vs equal temperament):
  in tune within +-14c for C3..C5 in every non-vibrato variant; C6/C7
  mostly locked (+-40c) but `default` C6 is a genuine multiphonic
  (2119 Hz + non-harmonic 2844 Hz), `pos_bridge` C6 wolfs (+735c) and its
  C7 is silent, `press_lo` fails high. The canonical model has the same
  high-register fragility our native harness showed — that is the
  calibration for "success."

## The STK model (per-tick, all from Bowed.h/cpp at 48 kHz)

```
bowVel = maxVelocity * ADSR                       maxVelocity = 0.03 + 0.2*amp
br     = -OnePole(bridgeDelay.lastOut)            pole 0.658125, gain 0.95
nr     = -neckDelay.lastOut
dv     = bowVel - (br + nr) = bowVel + LP(bridge) + neck
nv     = dv * BowTable(dv)                        rc = clamp((|(dv+off)*slope|+0.75)^-4, .01, .98)
neckDelay.tick(br + nv);  bridgeDelay.tick(nr + nv)
out    = 0.1248 * Body6(bridgeDelay.lastOut)      6 fitted biquad sections (Maestre)
```

Delay lengths: `base = sr/f - 4`; bridge = `base*beta`, neck =
`base*(1-beta)`; default beta 0.127236. Controls: pressure -> slope
`5 - 4*(v/128)`; position -> beta; vibrato adds
`base * vibGain * sin(2*pi*vibFreq*t)` to the NECK length only; bow
velocity/volume -> ADSR target; noteOn amp sets attack rate `amp*0.001`
per sample, noteOff release `(1-amp)*0.005`.

## Mapping to MForce

| STK piece | MForce | Notes |
|---|---|---|
| DelayL x2 | `DelayLine` (compensate OFF) | fractional linear read matches DelayL |
| length `(sr/f-4)*beta` | `ratio` pin <- `CurveNode` exprMode, 1 Linear knot `a=-4*beta/48000, b=beta` on `PerformNode(frequency)` | exact affine, existing primitives |
| OnePole string filter | **new `BiquadSource`** b0=0.324819 (=0.95*(1-p)), a1=-0.658125 | one node type covers one-poles and biquads |
| reflection negations | `CombinedSource(Multiply)` with -1 constant | for the -LP and -neck feeds into the delay inputs; the +LP/+neck feeds into dv reuse the same taps unnegated |
| BowTable | **new `BowTable`** node: out = x*clamp((\|(x+offset)*slope\|+0.75)^-4, min, max) | offset/slope are pins (pressure playable); min/max settings |
| Body: 6 biquads * 0.1248 | 6 `BiquadSource` in series; 0.1248 folded into section 0's b coefficients | raw fitted coefficients, verbatim from Bowed.cpp |
| ADSR bow envelope | `Envelope`, linear stages, maxValue = maxVelocity | |
| vibrato | `SineSource` -> scale -> `CombinedSource(Sum)` into neck ratio chain | phase 2 |
| loop closure | RefSource taps on both delays (tap z^-1 = STK lastOut read) | harness-documented pattern; delays advanced per advance-list rules |

## New engine nodes (reference-driven, multi-use, measurable)

1. **`BiquadSource`** — direct-form transposed or DF1 biquad,
   `y = b0 x + b1 x1 + b2 x2 - a1 y1 - a2 y2`. Settings (Float): b0, b1,
   b2, a1, a2 (defaults = identity: b0 1, rest 0). Input: `source`.
   Implements `phase_delay_at(f)` from H(e^jw) so compensated loops can
   contain it. Category: Modulator (a filter, but filters.h BW* are
   Modulator — follow whatever they use).
2. **`BowTable`** — Smith's memoryless friction curve as a graph citizen.
   Input `source` (= differential velocity); params `slope` (default 3.0,
   the pressure knob), `offset` (default 0.0); settings minOutput 0.01,
   maxOutput 0.98. Output is `x * rc(x)` (the new-velocity injection, so
   no external multiply is needed).

## Patch topology (single instrument-style patch, stk_bowed.json)

```
PerfFreq -> RatioBridge(CurveNode)  -> BridgeDelay.ratio
PerfFreq -> RatioNeck(CurveNode)    -> NeckDelay.ratio
BowEnv(Envelope) --\
LP(Biquad) <- tap(BridgeDelay) ------ Sum -> DeltaV -> BowTable -> NewVel
tap(NeckDelay) ----------------------/
NeckIn  = Sum( Mult(LP, -1),        NewVel ) -> NeckDelay.source
BridgeIn= Sum( Mult(tap(Neck), -1), NewVel ) -> BridgeDelay.source
Output  = Body0..Body5(Biquad chain) <- BridgeDelay (direct consumer)
```

z-budget: STK reads both delays' previous-tick outputs; MForce taps give
exactly that z^-1. The body chain reads the bridge delay's current-sample
output (STK: lastOut after tick), so it consumes the delay directly.
Residual off-by-one or filter-phase differences will appear as a constant
cents offset vs the reference table and get absorbed into the ratio
curve's -4 term (the STK constant is itself empirical).

## Validation (machine gates, no ears required)

Against the same 10-variant matrix rendered from the patch:
1. f0 per slot within +-10c OF THE STK REFERENCE's measured f0 (not of
   equal temperament — match the model, warts included).
2. RMS envelope per slot within 2 dB of reference (attack/sustain/release
   shape via 50 ms RMS frames, correlation > 0.95).
3. Harmonic profile: H1..H12 relative amplitudes within 3 dB where the
   reference slot is cleanly locked.
4. Character notes (not gates): default-C6 multiphonic, pos_bridge wolf,
   press_lo high-register collapse — report whether the port reproduces
   these regimes.

## Phases

1. Engine: BiquadSource + BowTable + registrations (+ null gate: both new,
   no existing patch touched -> gate must stay green by construction).
2. Generator `tools/gen_stk_bowed.py`: emits the patch JSON + renders the
   10-variant matrix + runs the comparison table automatically.
3. Iterate until gates pass; log residuals honestly.
4. THEN the MForce-native extensions (compensate ON for true intonation,
   hysteresis junction swap-in via Replace-with, morph between BowTable
   and stick/slip curves, body via formant bells) — each a sweep axis the
   proven base makes meaningful. Extensions are a follow-up round, not
   this port.

## Non-goals

- No changes to existing string-harness patches or hysteresis work.
- No attempt at byte-identical output (different engines); agreement is
  measured, thresholds above.
