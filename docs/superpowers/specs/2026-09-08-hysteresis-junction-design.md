# Hysteresis junction — stick/slip state on the Shaper

Status: SPEC DRAFT, 2026-09-08 (Fable 5, per Matt's go; awaiting Matt's
review — no implementation until approved).
Lineage: LOOP_PATCH_ANATOMY.md (junction feature column: "hysteresis
junction (stick/slip state — the bowed-string feature)"),
JUNCTION_OPERATING_POINT.md (origin asymmetry, threshold mechanics),
2026-09-05-curve-morph-design.md (A/B curve storage + bow preset this
design reuses), feedback_loop_design.md §3.5.
Sibling experiment: the resonant-valve (lip) probe needs NO engine work —
tools/gen_feedback_valve1.py, running as this is written. Lip = continuous
state (a filter, existing plumbing); bow = discrete state (a branch
decision carried between samples, which no node has). This spec adds the
latter.

## 1. Motivation

Every loop attack so far lands on the tube-to-reed spectrum regardless of
curve shape, and the anatomy doc names the mechanism: the junction is
memoryless — same input, same output, always. A bow is not. Its friction
has two regimes (stuck: bow and string move together; slipping: string
flies back under falling kinetic friction), and which regime you are in
depends on where you have been. That memory is what produces Helmholtz
motion — one stick/slip pair per period, the sawtooth-cornered waveform —
and no memoryless curve can produce it, only imitate its spectrum badly.

The r5c reframing already identified origin asymmetry as the "bowed/brassy
character" axis within the memoryless world (oboe_again's "little bit
bow-y"). This is the step past that ceiling.

Plain lesson: hysteresis = the system's response depends on its history,
not just its current input. A thermostat is the everyday example — the
switch-on and switch-off temperatures differ, and which one applies
depends on whether the heater is currently running. Here the "thermostat"
is the bow's grip, and the two temperatures are breakaway and recapture.

## 2. Model

Two curves + one bit of state, evaluated per sample in curve-x space
(s = drive · x, same windowing as today):

- **STUCK**: y = curveA(s). The stick curve — steep through the origin
  (strong coupling; the bow drags the string). Transition to SLIPPING
  when |s| > breakaway.
- **SLIPPING**: y = curveB(s). The slip curve — the falling-flank
  friction shape (the editor's `bow` preset is exactly this). Transition
  back to STUCK when s crosses zero, or |s| < capture if capture > 0.
- prepare() resets to STUCK (bow resting on the string at note start;
  SourcePool per-note state clearing covers voice reuse — r4 verified).

The loop mechanism this should produce: stick's steep slope regenerates
hard until the excursion exceeds breakaway; the slip curve's falling flank
sheds the energy; zero-cross recaptures; repeat — a relaxation cycle
locked to the delay's round trip. Level regulation moves from "knee
compression on one curve" to "regime switching between two," which is a
qualitatively different (and much more violent) waveform per period.

Thresholds are in post-drive units so they read directly against the
drawn curves, and both are ValueSources:

- **breakaway** — physically bow pressure (static friction × normal
  force). Envelope on it = digging in / lightening through the note.
  Modulating it near the oscillation amplitude should give the
  crunch/creak of over- and under-pressed bowing.
- **capture** — default 0 = zero-cross-only recapture (the physical
  rule: re-stick at the flyback). Nonzero = an early-recapture band,
  an exploration knob rather than a physical one.
- Constraint capture < breakaway; the gap is the hysteresis width.

## 3. Where it lives: Shaper mode, not a new node

The Shaper already stores two curves (`values`/`values2` + `segs`/`segs2`
from the morph work), has the A/B editor workflow, the per-segment shapes
that make the slip flank drawable, and the loader/UI plumbing. A new node
would duplicate all of it to add one bool of state.

- New Shaper setting `"hysteresis"` (bool, default false). When true:
  curve A = stick, curve B = slip, and `map()` routes through the state
  machine above.
- **Mutually exclusive with morph in v1**: hysteresis && morph > 0 is a
  loader warning, morph ignored. (Morphing the stick curve while
  hysteresis runs is a real v2 idea — playable bow character — but it
  multiplies the test surface now.)
- Unlike morph, hysteresis needs NO matching point counts between A and B
  (nothing lerps; the curves are just alternatives). Editor A/B labels
  become Stick/Slip in this mode.
- New params `breakaway` (default 0.6, range 0..2) and `capture`
  (default 0, range 0..2), ValueSource-wired like drive.
- hysteresis=false is byte-identical to today — null gate unaffected,
  no patch migration.

## 4. Discontinuity and aliasing, stated up front

A regime switch is a step from curveA(s) to curveB(s) at the transition
sample. One switch pair per period IS the sawtooth corner — that
discontinuity is the sound, not an artifact. But it is also wideband, and
the loop's aliasing ladder (anatomy doc) applies with a caveat: **ADAA
does not cover state-switched curves** (the antiderivative trick assumes
one memoryless map). So the mitigation ladder for this node is: quantify
via the free 192 kHz A/B first; keytrack breakaway/drive to tame top
octaves second; oversampled loop region third. Expect high notes to be
the problem register, same as every previous alias complaint.

Also expected: the slip curve is genuinely asymmetric (friction opposes
motion in one direction), so per-pass DC deposit is structural, not
incidental — DC_Block_hpf becomes load-bearing exactly as the junction
doc predicted for asymmetric curves. Any "is dcblock necessary" test
must exclude hysteresis patches.

## 5. What v1 deliberately leaves out

- **Directional breakaway** (|s| test vs one-sided s > +breakaway): real
  bows drag one way. v1 uses magnitude and lets the drawn curve
  asymmetry carry direction; if renders show symmetric double-slip per
  period (two corners where Helmholtz has one), a signed breakaway is
  the first revision. Flagged as the likeliest v1.1.
- **Soft/stochastic breakaway** (breakaway jitter = bow-hair noise,
  creak texture). Expressible later as a noise ValueSource summed into
  the breakaway wire — existing primitives, no feature.
- **Thermal/elasto-plastic friction** (state as a continuous bristle
  variable). Real literature, real quality gains, order-of-magnitude
  more machinery. Not until the two-state version has been heard.

## 6. Validation plan

1. Null gate 196/196 (hysteresis absent everywhere).
2. Unit-level: feed a slow sine ramp through a hysteresis Shaper
   open-loop; assert the output traces A up to breakaway, B back to the
   crossing — the textbook hysteresis loop, cheap to assert numerically.
3. In-loop probe harness (junction-referenced criticals, per
   gen_feedback_inloop1/valve1 pattern) on oboe_default-with-bow-curves:
   stick = steepened origin curve, slip = editor bow preset.
4. The honest listening control: the SAME two curves flattened into the
   best memoryless approximation (hysteresis off, values = slip curve) —
   the r4-era bow patches. If hysteresis-on does not clearly beat that
   A/B, the feature failed its reason to exist and we say so.
5. Waveform check on the loop tap: slope asymmetry / corner count per
   period (Helmholtz = one), measurable offline from the render, no
   debug taps needed.

## 7. Open questions for Matt

1. Magnitude vs directional breakaway in v1 (§5 — I lean magnitude
   first, revise on evidence; cheap to flip).
2. Param names: breakaway/capture vs pressure/grip vs stick/slip
   thresholds — UI copy question, engine doesn't care.
3. Does the Stick/Slip A/B relabel earn a distinct junction face badge
   in the editor, or is the setting checkbox enough?
