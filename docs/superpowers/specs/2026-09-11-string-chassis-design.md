# String chassis — the bowed string built from the published model

Status: SPEC DRAFT, 2026-09-11 (Fable 5, per Matt's "ready for the
bow-native skeleton using actual string physics"). Awaiting Matt's
review / weekend brainstorm — no build until approved.
Method note applies (LOOP_PATCH_ANATOMY.md): this spec STARTS from the
state of the art — the digital waveguide bowed string (J.O. Smith,
CCRMA; McIntyre/Schumacher/Woodhouse for the friction/hysteresis
physics) — and defines its MForce ValueSource-graph expression. Our
contribution surface is the translation plus sweep-discovered
refinements, not the model.

## 1. The published model (what we are expressing)

Smith's bowed-string waveguide, in its standard reduced form:

- The string is TWO waveguide segments meeting at the bow point:
  bridge side (fraction p of the length, p ~ 1/8..1/6 typical bowing
  position) and nut side (1−p). Each segment is a delay; each far end
  reflects with inversion — the nut near-losslessly, the bridge
  through a LOSSY LOWPASS (all the string's damping is traditionally
  lumped there).
- The bow couples at the junction through a FRICTION CURVE on
  Δv = v_string − v_bow: steep static-friction region around Δv = 0
  (stick), falling kinetic flanks (slip), with the MSW hysteresis rule
  choosing branches — exactly our hysteresis Shaper + bias.
- Helmholtz motion arises because the corner launched at a slip
  travels the string and its RETURN to the bow point triggers the next
  transition — one slip per period, natively. No subharmonic
  architecture (the bow rounds' period-2 disease cannot exist here by
  construction).
- The OUTPUT is transverse force at the bridge (the bridge-side
  signal), radiated through the instrument body's resonances.
- The bowing-point comb: harmonics near multiples of 1/p are weak
  (the bow sits still for them) — bow position is a first-class
  timbre control the single-loop retrofits could never express.

## 2. MForce translation (existing primitives throughout)

Two DelayLines closed into ONE cycle through the junction:

  Junction (hysteresis Shaper, round-5 recipe) 
    -> BridgeDelay (length p * N)   -> BridgeRefl (SVF LP, INVERTING)
    -> NutDelay   (length (1-p)*N)  -> NutRefl (inverting, lossless-ish)
    -> tap closes back into Junction input,
  with Bow_bias summed at the junction input (bow velocity, envelope =
  the stroke) exactly as in bow rounds 2-5.

Concrete node list (one candidate wiring; brainstorm may refactor):
- Bow_sum   = CombinedSource Sum: {tap: NutDelay} + Bow_bias
- Junction  = Shaper, hysteresis on, STICK/SLIP curves, breakaway ~0.6,
  capture in the round-5 period-1 regime (band reaches the operating
  point), drive pinned 1.0
- BridgeDelay = DelayLine ratio p (of the full period), source Junction
- BridgeRefl  = SVF lowpass (the string's lumped loss, gentle — start
  ~8-10 x f0 keytracked, res ~0.5 floor) followed by an INVERTER
  (Shaper y = −x: values [-1,1, 1,-1]) — reflection with sign flip
- NutDelay    = DelayLine ratio (1−p), source BridgeRefl
- NutRefl     = inverter Shaper only (lossless nut; both inversions
  per full cycle multiply to +1, preserving the Helmholtz sign
  convention while each segment end inverts as physics says)
- Output      = BridgeDelay tap (force at the bridge) -> Body (2-3
  fixed SVF resonances, violin-plate-ish placement to be swept; NOT
  the oboe formants) -> Reverb light
- NO WhiteNoise summed anywhere. Bow-hair noise = a small noise source
  wired into breakaway (or bias) — friction jitter, per the taxonomy.

### Tuning (the one genuinely new engineering question)

The cycle contains TWO delays; `compensate` was designed for
single-delay cycles ("parallel feedback paths sum their reported
lags... only meaningful for a single path"), and DelayLine does not
report its own length via phase_delay_at, so neither delay can see the
other's bulk latency. Plan: compensate OFF on both; total length =
sr/f0 = pN + (1−p)N + (members' phase delays + 2 tap samples), with
the correction folded into the two ratios by the generator (we know
every member's phase_delay_at analytically from the walk work; the
round-1..5 measurement harness verifies per note to ±30c before any
queue ships). If hand-tuning proves fiddly, backlog 65/66's eventual
engine work is the principled fix; we do NOT block on it.

## 3. What carries over from the bow rounds

- Period-1 junction recipe (round 5): capture band reaching the bias
  operating point = one slip per trip. Here the returning corner
  should do the triggering — the capture setting may relax; sweep it.
- Bias/breakaway = bow velocity/pressure envelopes = the entire
  articulation surface (no drive/breath envelopes exist in this
  chassis at all).
- All measurement discipline: criticals at shipped root, per-note
  pitch/fire before "queue ready", corners/period (target ~2), and the
  A/B controls: best round-5 cell + a memoryless-friction variant
  (curve without hysteresis) as the kill-test.

## 4. Round 1 axes (proposed, ~12 cells)

p (bow position) {1/6, 1/8, 1/12} x capture {round-5 winner, half of
it} x bridge-loss cutoff {8, 12 x f0}. Fixed: bias 0.6 x ba, fast
engage, gain 1.15-1.4 x measured critical. C3..C7 renders + QWERTY
patches, standard queues (hysteresis_string1 family — new family name;
the bow retrofit family is closed).

## 5. Open questions for Matt / brainstorm

1. Is the two-delay tuning plan (compensate off, generator-computed
   ratios, measured verification) acceptable as v1, or gate on the
   65/66 engine work first?
2. Body: sweep 2-3 fixed resonances now, or bare bridge output first
   and add body only after the mechanism verdict? (My lean: bare
   first — one variable at a time; the bow rounds punished stacked
   unknowns.)
3. Bow-hair noise into breakaway vs bias — or omit entirely for
   round 1? (My lean: omit; add on a second round only if the clean
   mechanism sounds sterile.)

## Addendum 2026-09-12 � harness campaign framing (Matt's directive)

This spec is now one of TWO sibling harnesses (with
2026-09-12-brass-harness-design.md) built per Matt's directive:
state-of-the-art first, sweep 2-3 axes, ML-ears until probable
success. Research refresh (see sources in the session record):
the two-rail waveguide + friction-curve junction remains the
real-time standard; the published realism ladder above it is
elasto-plastic (bristle-state) and thermal/finite-width friction
(Serafin/Avanzini; Woodhouse) - CONTINUOUS friction state, not
expressible in current primitives. v1 builds the standard structure
with ZERO engine code (hysteresis Shaper = the friction junction,
round-5 period-1 capture recipe as the starting setting); the
elasto-plastic junction is the measurement-gated engine upgrade if
v1's stick-slip texture fails ML ears / Matt's ears.

Beyond replication (MForce levers): bow position p as a modulatable
pin (audio-rate flautando/sul-pont morphs no player can do), bias =
bow velocity as a drawn stroke envelope, breakaway noise = hair
texture, duration field pacing strokes (backlog 64). ML ears scoring:
viola config (research/ml_ears/configs/viola.json, reference built).
