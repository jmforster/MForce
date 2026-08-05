# SlewLimiterSource — design (backlog 3g)

Date: 2026-08-05 · Dipsy · dsp lane

## Why

Run 20 explained the t3_23 clicks: velvet impulses drive FMSource's `phase`,
so each impulse sign-flips the carrier for exactly ONE sample. That is a
step discontinuity — maximally bright, and the only dial on its brightness
today is jump SIZE (which also changes pitch content) or density.

A rate limiter between the impulse source and the phase pin turns each step
into a short ramp. A phase ramp is a frequency offset, so a click becomes a
short chirp whose length — and therefore brightness — is set by one number.
That is the tunable-brightness dial REVIEW 14 asks for, and it is reusable
anywhere a control signal is too steppy (segment sources, sample-and-hold,
quantised envelopes).

## What

New `SlewLimiterSource` (`engine/include/mforce/source/slew_limiter_source.h`),
category Filter, registered as `SlewLimiterSource`.

Input: `source`.
Params (ValueSources, so everything is envelope-able):
- `rate` — units per second. Default 1000, min 0, max 1e6 (deliberately
  wide; rule-breaking rates are allowed).
- `fallRate` — units per second on the falling side. Default 0, which means
  "mirror `rate`". A genuinely-zero fall rate (freeze on the way down) is a
  degenerate case; express it as a tiny positive number.

Config:
- `mode` — Int enum, 0 = `Slew` (default), 1 = `Lag`, 2 = `Peak`.

### Correction found during implementation

The motivating premise above is WRONG about the click source, and the fix is
a third mode. The velvet source emits one-sample IMPULSES, not steps: the
signal is 0, then ±0.5 for one sample, then 0 again. A sign-keyed rate
limiter turns that into a one-sample impulse of height `rate*dt` — it
attenuates the click, it does not stretch it. Rendering a rate ladder on the
naive insertion would have measured nothing but a level drop.

`Peak` mode is magnitude-keyed instead: while `|in| > |held|` it attacks
toward the input at `rate`, otherwise it decays toward ZERO at `fallRate`.
An impulse therefore becomes a jump plus a glide back to rest, symmetric for
both polarities. A linear phase glide is a constant frequency offset, so
that glide is the chirp — and `fallRate` is its length, i.e. the brightness
dial. This also makes the node a general envelope follower.

### Semantics

Let `dt = 1/sampleRate`, `d = input - held`.

- **Slew**: `|d|` is clamped to `r*dt` per sample, so the output is a
  straight ramp. A step of size S takes `S/r` seconds. Arrival is exact.
- **Lag**: `held += d * clamp(r*dt, 0, 1)` — a one-pole, exponential
  approach, never exactly arriving. Duller onset, no corner at the top.

`rate` therefore has different units per mode (units/s vs 1/s) but the same
DIRECTION in both — bigger is faster — which is what a UI sweep needs. The
hint string says `u/s|1/s`.

### Priming

`prepare()` clears a `primed_` flag; the first `next()` after prepare adopts
the input value verbatim. Without this every note would open with a ramp
from 0, which is an artifact, not a feature.

## Real-time safety

No allocation, no branching on anything but sign, no libm. Three virtual
`next()` calls per sample (source + 2 params) — same shape as every other
wrapping modulator in the engine.

## Verification

1. Build cli + ui, `mforce_ui --stamp` exit 0.
2. Null test: every existing patch byte-identical (new type, zero existing
   users — a partition check, not a claim).
3. Step-response unit render: a known step through Slew at a known rate,
   measured rise time vs `S/r`. Assert, don't assert-by-eye.
4. Click patches: `poc*` re-rendered with the limiter in the phase path at
   a rate ladder; spectral centroid must fall monotonically as rate falls.
   Ladder queued for REVIEW (taste), centroid measured (metric).
