# Brass harness — outward-striking lip on a long bore

Status: SPEC, 2026-09-12 (Matt's directive: spec both harnesses as
siblings to the reed/flute harness, state-of-the-art first, build what
is required, sweep + ML-ears until probable success).
Siblings: reed/flute = the proven wind chassis (LOOP_PATCH_ANATOMY);
2026-09-11-string-chassis-design.md (the other new harness).
Supersedes the 2D-junction spec's §2 gate round: THIS harness round IS
the gate, run in a brass-seated chassis per that spec's 09-11 addendum.

## 1. Published model (what we express)

Standard structure (CCRMA/JOS "Brasses"; numerical-continuation
trumpet studies; FDTD brass environments):
- A one-degree-of-freedom OUTWARD-STRIKING lip valve — a mass-spring
  resonator tuned near (conventionally slightly below) the sounding
  frequency, whose displacement gates the flow: rising mouthpiece
  pressure OPENS the lips (a reed closes — opposite coupling sign).
- The bore is long: the played note is mode n of the air column (a
  trumpet C5 rides a bore fundamental near C3). The lip resonance
  SELECTS the mode. (valve1's "mode pulling" was this.)
- Mouthpiece cup = fixed Helmholtz emphasis (~300-1000 Hz) between
  lips and bore.
- Bell = frequency-dependent boundary: reflects lows back into the
  bore, radiates highs — loop filter and output filter are
  COMPLEMENTARY branches of one signal.
- Loud "brassiness" = nonlinear wave steepening in the bore (shock
  formation); modeled in waveguides via modulated fractional delays —
  we approximate later, if at all, with mild distributed in-loop
  shaping (deferred, spec §5-style).

## 2. MForce expression (zero engine code for v1)

The lip's LINEAR half is an SVF bandpass (a mass-spring-damper — the
valve rounds' machinery); the lip's GATING is curve-shape coupling:
the Shaper's per-sample morph pin blends curve A (lips near closed)
toward curve B (lips open) driven by the lip resonator's displacement.
That is f(x, s) with a 2-point basis — the 2D junction's N=2 slice,
which shipped with the curve-morph work. Backlog 66's fix (inputs-only
compensation walk) already keeps this control chain out of the tuning.

Node sketch (oboe1-derived generator, heavily reshaped):
- Mouth_pressure = Envelope (breath pressure — brass keeps the wind
  chassis' pressure-source energy entry, NOT the string's silence)
- Lip = SVF Bandpass, keytracked LIPTUNE x played f0, res Q, source =
  junction input sum (mouthpiece pressure = bore return + mouth)
- LipOpen = Shaper rescale of Lip displacement to 0..1
- Junction = Shaper: values = closed-lip curve (low flow, hard
  clamp), values2 = open-lip curve (generous flow), morph <- LipOpen.
  OUTWARD-striking sign: the curves are drawn so rising pressure x
  with open lips passes MORE (this is what distinguishes the pair
  from a reed's, where rising pressure chokes flow).
- Cup = SVF bandpass, FIXED ~500 Hz (not keytracked), in the input sum
- Bore = DelayLine at ratio MODE (2, 3, 4 = playing mode n of a long
  bore), compensate on (single-delay cycle — unlike the string)
- Bell crossover: BellRefl = SVF LP (loop branch, cutoff the bell's
  cutoff ~ 700-1500 Hz fixed) closing the loop; output = the
  COMPLEMENT (Combined: bore signal - LP branch, i.e. the radiated
  highs) -> light body/room. One signal, two complementary paths.
- Damp keytrack of the wind chassis is REPLACED by the bell LP (fixed,
  not f0-tracked — brass bores are not tone-hole instruments).

## 3. Beyond replication (MForce levers the literature lacks)

Every one of these is a ValueSource pin, so all are modulatable
per-note or at audio rate — real instruments cannot do this:
- LIPTUNE as a played parameter (lip glissando across modes; the
  "cracked note" as an articulation, on purpose);
- morph-coupling DEPTH as dynamics (whisper-brass to full buzz);
- bell cutoff as a continuous mute (harmon/cup mutes are bell
  transforms);
- mode ratio as a discrete pitch lever (natural-horn arpeggios on one
  fingering);
- duration perform field pacing the pressure envelope (backlog 64).

## 4. Round 1 axes (~12-18 cells)

LIPTUNE {0.85, 1.0} x Q {5, 15} x MODE {2, 3} at fixed cup/bell;
criticals bisected per (liptune, Q, mode) at the shipped root; drive
band transplanted as ratios. Measured gates before any queue: per-note
pitch (the mode must LOCK to the played note — the round fails
honestly if lip selection can't hold the mode), fire 5/5, then ML ears
vs the trumpet_Bb reference (config + reference to be built — samples
exist in research/inst_samples/trumpet_Bb).

## 5. Engine upgrades, measurement-gated

- N-station curve stack + state pin (2D junction spec §3) if the N=2
  morph slice measurably saturates (curve-pair sweeps all landing in
  one basin).
- Modulated-fractional-delay brassiness if ML ears' high-band terms
  stay flat vs the ff trumpet reference at any curve/drive setting.

## 6. Documentation deliverables (per Matt's directive)

Differences-to-reed table, component notes, and the hand-tweak guide
(which knobs move what, with the predicted sweet regions) land in
docs/research/feedback_sweeps/BRASS_HARNESS_NOTES.md after round 1's
measurements exist to ground them.
