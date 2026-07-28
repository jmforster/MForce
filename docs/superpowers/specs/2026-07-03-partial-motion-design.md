# Partial Motion layer — design spec (Fable1 task)

Date: 2026-07-03
Goal: (1) chaotic attack that settles into sustained timbre; (2) continued,
*evolving* partial movement over the sustain ("shimmer"). See docs/Fable1.md.

## Diagnosis of prior attempts

- `detune` ramp failed because each partial gets an independent static random
  offset — independent movement makes the ear segregate partials into separate
  notes (auditory common-fate grouping).
- `bandwidth` enhancement (per-partial AM noise) is fully independent across
  partials and amplitude-only — it adds fuzz around spectral lines but no
  *frequency-domain* line movement, and no cross-partial structure.
- Everything else in Partials is a two-endpoint lerp (`_1 → _2` blended by an
  env). Movement is a single monotonic trajectory; nothing in the engine can
  produce motion whose *character changes over time*.

Design principle: make **coherence** and **non-stationarity** first-class.
All movement is built from smoothed random walks with (a) a shared/independent
mix (coherence) and (b) meta-randomized segment timing/depth (evolve).

## New features (all in `Partials` base, all default-inert)

### 1. Frequency motion (line-broadening walk)
Per-partial pitch offset in cents, applied as `pfreq *= 2^(cents/1200)`.

- `motionDepth1` / `motionDepth2` — cents, blended by param `motionEnv`
  (same idiom as `bwEnv`): env≈1 → depth2, env≈0 → depth1. Default 0/0.
- `motionHz` — walk segment rate (default 4).
- `motionCoherence` 0..1 — mix of one shared walk vs per-partial walks
  (1 = all partials move together in cents ⇒ proportional Hz, fuses like
  vibrato; 0 = independent, the known segregation failure — kept for A/B).
- `motionEvolve` 0..1 — meta-modulation: each new walk segment randomizes its
  own length and amplitude, so the movement's character wanders.
- `motionScale` — exponent on the partial multiplier scaling depth
  (0 = equal cents everywhere; >0 = upper partials wander more).

Chaotic attack = wire `motionEnv` to a fast-decay per-note envelope with
depth2 large (e.g. 60–120 cents) and depth1 small (2–8 cents): scattered
lines converge onto the harmonic grid *while staying partially coherent*.

### 2. Amplitude shimmer
Same walk machinery on per-partial gain: `gain_i = max(0, 1 + depth·walk_i)`.

- `shimmerDepth1` / `shimmerDepth2` (0..1, param `shimmerEnv`),
  `shimmerHz` (default 3), `shimmerCoherence` (default 0),
  `shimmerEvolve` (0..1).

### 3. Onset dispersion
Real bowed/blown notes assemble their spectrum over time. Each partial gets a
per-note random onset delay + short fade-in (pure amplitude; no extra noise
source, so nothing reads as "crossfaded noise").

- `onsetSpread` — max delay seconds (default 0 = off).
- `onsetTilt` -1..1 — orders delays by partial height (+1 low partials first
  / high bloom later; -1 reverse; 0 random).
- `onsetFade` — fade-in seconds per partial (default 0.03).

Needs a per-note sample counter (reset in `partials_prepare`, incremented in
`partials_next`). Delays re-randomized each note.

### 4. Energy trading
Adjacent partial pairs share one slow walk `w ∈ [-1,1]`; gains `1 ± depth·w`.
Spectral energy sloshes between neighbors while the sum stays ~constant —
movement *inside* the spectrum without loudness pumping.

- `tradeDepth` (0..1, default 0), `tradeHz` (default 2).

## Implementation notes

- All state allocated in `partials_prepare` (no heap in the render loop).
- Walks advance once per sample in `partials_next()` into per-partial value
  arrays; `get_partial_value` just reads them.
- Walk primitive = the existing bw idiom: current→target lerp with smoothstep;
  evolve multiplies each segment's length by `(1+3e)^u, u∈[-1,1]` and its
  amplitude by `1 + 0.7·e·u'`.
- New configs added to `Partials::config_descriptors` **and** each subclass
  list (FullPartials / SequencePartials / ExplicitPartials duplicate the base
  entries — established pattern). `motionEnv`/`shimmerEnv` go in the base
  `param_descriptors` only (subclasses don't override it).
- No patch back-compat concerns (defaults inert; no good patches to protect).

## The 10 patches (patches/fable1/, renders/fable1/)

Base = viola_instrument.json (48 explicit harmonics + BandSpectrum body +
Vibrato), score = C major scale C4→C5, 8 notes × 1.6 s.

| # | name | techniques |
|---|------|-----------|
| 01 | mot_settle | motion attack: depth 90→4 cents via fast-decay env, coherence 0.6 |
| 02 | mot_coherent | sustain shimmer: 6 cents, coherence 0.9, evolve 0.6 |
| 03 | mot_incoherent | same as 02 but coherence 0 — A/B of the segregation theory |
| 04 | onset_bloom | onsetSpread 0.15, tilt +1 (harmonics bloom upward) |
| 05 | onset_scatter | onsetSpread 0.25 random + motion settle 60→5 cents |
| 06 | trade_slosh | tradeDepth 0.5 @ 1.2 Hz + mild coherent motion |
| 07 | shimmer_amp | amplitude shimmer 0.45, coherence 0.3, evolve 0.5 |
| 08 | expand_converge | existing ExpandRule: wide/detuned cluster → clean via multEnv |
| 09 | bw_coupled_attack | existing bandwidth env 0.8→0 fast + onset dispersion + motion |
| 10 | kitchen_sink | onset bloom + motion settle + evolve + trade + light shimmer |
