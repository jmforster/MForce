# STK port campaign — running notes

Steal-first campaign (steering meeting 2026-09-13). Spec:
docs/superpowers/specs/2026-09-13-stk-bowed-port-design.md. Reference
code: sibling checkout `C:/@dev/repos/stk`. Reference driver:
tools/stk_ref/ (minimal STK compile, no RtAudio; deterministic renders at
48 kHz into renders/scratch/stk_ref/).

## Bowed — PORTED AND VALIDATED 2026-09-13

New engine nodes (registered, generic wiring, no special-case loader code):
- **Biquad** (engine/include/mforce/filter/biquad_source.h) — raw
  b0/b1/b2/a1/a2 sections as settings; phase_delay_at from closed-form
  H(e^jw) with polarity folded out. Covers STK's OnePole string loss
  (b1=b2=a2=0) and the six fitted Maestre violin-body sections.
- **BowTable** (engine/include/mforce/source/bow_table_source.h) — Smith
  (1986) friction curve verbatim from STK; outputs dv*rc(dv); slope
  (= bow pressure) and offset are live pins.

Patch topology: two DelayLines (compensate OFF, STK's literal
`(sr/f - 4)*beta` lengths via CurveNode exprMode affine ratio), taps on
both delays mirroring STK's lastOut reads, body chain advances
BridgeDelay, NeckDelay advances via the tap-only advance list. Generator
+ comparison harness: tools/gen_stk_bowed.py. Patches in
patches/sweep/stk_bowed_port/, renders in
renders/dsp/sweep/stk_bowed_port/.

### Results vs ground truth (10 variants x C3..C7)

- **f0: +0.0c against the STK reference on 43/43 locked non-vibrato
  slots** — including reproducing the reference's own pathologies at the
  same frequencies: default-C6 multiphonic, pos_bridge-C6 wolf. The two
  slots where the reference is silent (press_lo C7, pos_bridge C7) are
  silent in the port too (RMS 0.0 both).
- Envelope correlation >= 0.974 on all non-vibrato slots (mostly 1.000);
  gain fit uniform 0.396-0.405 = exactly volume 0.7 x pan 0.7071 x
  velocity 0.8 — level chain fully accounted for.
- Harmonic profile (H1..H8 rel dB): <= 2.4 dB on all healthy variants.
  Larger deviations (up to 13 dB) only in the reference's chaotic corners
  (press_lo C3/C5/C6, pos_bridge C3) and the wolf C6 levels — regimes
  where trajectory sensitivity makes exact amplitude non-reproducible.
- Vibrato variant locks C3..C7; C5/C6 read +20/-36c and env corr ~0.83
  because STK's vibrato LFO free-runs across notes while MForce voices
  reset phase per note — measurement smear plus genuinely different FM
  phase, not a model error.

### Loose ends / lessons

- Loader resolves refs BACKWARD in node order: a forward {"ref"} is
  "Unresolved node ref". Taps are the only forward references allowed.
  (Cost one iteration on the vibrato variant.)
- The two-rail waveguide with cross-coupled taps worked FIRST TRY with
  the documented tap/advance-list semantics — no ordering residual
  visible at the f0 level (backlog 65's ±1-sample concern did not bite
  here).
- Secondary reference: JOS Mohonk05 bowed-string examples (14 WAVs +
  legend page) archived at renders/scratch/stk_ref/mohonk05/ — feature
  variants (bow table, torsion, body, dispersion) beyond STK Bowed,
  i.e. the menu of NEXT features the extension rounds could add.

### Extension round (NOT this port — follow-up sweep axes)

compensate ON (true intonation vs STK's -4 heuristic); hysteresis
junction swap via Replace-with (stick/slip vs memoryless BowTable A/B);
morph pin between bow curves; formant-bell body vs Maestre biquads;
keytracked string loss.

## Clarinet — PORTED AND VALIDATED 2026-09-13

ZERO new engine nodes. OneZero = Biquad(0.5, 0.5); ReedTable (linear,
clamped +-1) = a two-point Shaper curve, exact; breath = Envelope *
(1 + noise + vibrato) via CombinedSources. Delay ratio(f) = 0.5 - 1.5f/sr
(OneZero's 0.5-sample linear phase + lastOut). Files: clarinet_ref.cpp,
gen_stk_clarinet.py, patches/sweep/stk_clarinet_port/.

Results: **f0 +0.0c on all 40 slots.** noise_off variant is the true
fidelity gate (STK Noise seeds from time(), realizations never match):
envCorr >= 0.97, harmonic profile <= 0.09 dB — exact. Noisy variants
show up to 17 dB harmonic wobble = noise realization, not model error;
reed_hard C6/C7 envCorr ~0.5 = squeak-onset timing chaos under noise.

## Flute — PORTED AND VALIDATED 2026-09-13

ZERO new engine nodes. Jet x^3 - x from two Multiplies + an identity
Shaper as the +-1 saturator; OnePole bore loss (pole 0.6540625, negation
folded into b0) and DC blocker (1,-1 / 1,-0.99) as Biquads. The one-pole
phase delay is not affine in f, so the two delay ratios use points-mode
CurveNodes with knots at every semitone C2..C8 computed from the exact
atan2 formula (exact at playable notes). Overblow constant 0.66666 and
jetRatio (default 0.32) live in the knot tables. Files: flute_ref.cpp,
gen_stk_flute.py, patches/sweep/stk_flute_port/.

Results: noise_off = **+0.0c all slots, envCorr >= 0.956, Hprof
<= 0.11 dB — exact.** Noisy variants +0.0c nearly everywhere; outliers
(noise_hi C3 -702c, jet_lo C7) are slots where jet noise itself selects
the register — both sides lock, on different realizations. That is
flute physics, not port error.

## Brass — PORTED AND VALIDATED 2026-09-13 (matches a barely-speaking reference)

Engine: **Biquad gained a resonance mode** (settings mode=1; frequency/
radius pins; a1 = -2r cos(2 pi f/sr), a2 = r^2 per sample, b0 = gain) —
the keytracked lip filter, verbatim STK setResonance. Lip^2 + upper
clamp (identity Shaper on [0,1]) = position->area; scattering =
bore + dp*(mouth - bore) crossfade; DC blocker biquad inside the loop;
slide ratio(f) = 2 + 3f/sr. KNOWN DEVIATION: STK DelayA (allpass) vs
MForce linear interp — invisible at the f0 level in every locked slot.
Files: brass_ref.cpp, gen_stk_brass.py, patches/sweep/stk_brass_port/.

Results: the STK reference ITSELF barely speaks at 48 kHz under its own
noteOn drive (amp 0.8 -> mouth 0.24 vs bore reflection 0.85): C3..C5
silent in most variants, C6 default = 97 Hz sub-rumble, real locks only
at hard C6/C7, lip_lo C6/C7, lip_hi C5, soft C5/C6. **The port
reproduces ALL of it**: every silence, the same 96.97 Hz rumble, same
locks at +0.0c, envCorr 1.000 across the board (one chaotic exception:
vibrato C5, additive vibrato wiggling the ignition threshold — one side
speaks, the other doesn't). Independently corroborates the native brass
harness finding that ignition is a narrow drive window. Drive/ignition
exploration belongs to the extension round, where MForce can push
beyond STK's control ranges.

## THE 48k BASELINE BUG (Matt's ear, 2026-09-13 evening)

Matt played a passage on the bowed port, compared it to the Mohonk
page's stock `bowed.wav`, and called the baseline bogus. He was right —
and the page's legend also clarifies what the site samples ARE: cello
performances by Peder Larson; `bowed.wav` = stock STK Bowed, everything
else = stock + research features (hyperbolic junction f / finite bow
width b / torsion t / body filter / dispersion d).

Root cause, measured not theorized: **STK carries fixed filter
coefficients designed at its native 22050 Hz.** The Maestre body
sections put resonances at 261/525/1072/1287/1839 Hz at 22050 (violin
body); run verbatim at 48 kHz they land at 569..4004 Hz (nasal
nonsense). Related rate casualties: brass lip bandwidth doubles in Hz
at 48k (measured: STK brass at 22050 SPEAKS at C4/C5 where 48k is
silent — the "ignition-limited" reading was partly a rate artifact);
clarinet's OneZero corner scales with fs (brighter than canonical).
The pole FORMULAS (string filter, flute filter) adapt; the fixed
numbers don't.

Fixes landed:
- All four reference drivers take a sample-rate argument; canonical
  22050 references in renders/scratch/stk_ref/{bowed22k,brass22k}.
- gen_stk_bowed.py: **body48 correction** (default; --verbatim keeps
  the STK-null form): every body pole/zero z -> z^(22050/48000) — same
  physical Hz, same bandwidth in Hz — plus one global gain matched at
  the 525 Hz peak. Gate: corrected cascade matches the native 22050
  body response to **0.46 dB max / 0.15 dB mean over 100-4000 Hz**.
  f0 gates unchanged (+0.0c); Hprof-vs-48k-reference now deviates BY
  DESIGN.
- Audition pair restaged: bowed_port (body48) vs bowed_REF (STK@22050).

Still queued: brass lip-radius rate correction (radius -> r^(22050/48000)
keeps lip bandwidth in Hz; then re-probe ignition at 48k), clarinet
OneZero corner correction, flute check. Lesson for every future port:
**ask of each constant "is this a physical quantity or a
normalized-frequency quantity" — normalized ones must be re-realized at
the target rate.**

## CANONICAL MODE (Matt's round-2 ears: "high harmonic drowns the
## fundamental in sustain" + "clicks like envelope issues")

Both complaints measured, root-caused, fixed (2026-09-13 late):

1. **Regime bug (the high harmonic).** STK@48k itself double-slips:
   H2/H4 sit +31..+46 dB over H1 at C3/C4 where STK@22050 plays clean
   Helmholtz (H2 +18.6). Causes: per-sample ADSR rates halve gesture
   times at 48k (the transient picks the limit cycle — the hysteresis
   campaign's own lesson), and the string-filter formula under-darkens
   (cutoff ~3.2 kHz at 48k vs canonical ~2.1 kHz). The port had
   faithfully replicated a rate-warped regime.
2. **Clicks.** Two kinds: the original patch's envelope used LITERAL
   seconds (1.67 s sustain) — any shorter Passage note truncated
   mid-sustain; and even with adaptive stages, the voice cut at note end
   truncates the still-ringing string (measured 0.044 step exactly at
   note end; rc stays ~0.98 after bow-off, ring ~-80 dB/s — STK's own
   driver truncates the same way).

**gen_stk_bowed.py default is now CANONICAL mode** (--verbatim keeps the
STK@48k null form): string pole 0.55^(22050/48000)=0.7600 (same cutoff
Hz, same 0.95 DC loss), gestures in 22050-seconds, duration-adaptive
envelope (pinned attack/decay/release seconds, expand sustain — Passage-
safe), delay comp = 4*(48000/22050) plus an f-interpolated calibration
table (COMP_DELTA, 2 iterations against the 22050 reference), body48
sections, and a LoopGate envelope on NeckDelay read-gain closing the
loop over the last 90 ms (MForce-native click fix; departs from STK,
which just truncates). Patches: stk_bowed_<v>_canon.json.

Canon results vs the 22050 reference: **C3-C5 within +-5c on nearly
every variant, envCorr >= 0.98; C3/C4 harmonic profile matches within
~1 dB per harmonic (port +18.7/+0.5/-3.5 vs ref +18.6/+0.2/-4.0)**;
note-end click gone (max residual step 0.018, a normal attack
transient). C6/C7 remain regime-fragile in several variants — they are
fragile in the 22050 reference too (C7 silent); the model's musical
band is C2-C5. Roundtrip 24/24 clean.

## LEGATO MECHANISM (Matt's round-3 ears: "theirs sounds legato, ours
## staccato" on 8ths — the articulation topic arrives on schedule)

Verified, not theorized: STK performances slur by CHANGING FREQUENCY ON
THE SOUNDING INSTRUMENT (CC101/setFrequency, bow held — no noteOff, no
clear). The string state persists; only delay lengths change. Driver
gained a `legato` variant (one noteOn, CC101 steps): rise-to-80%-of-
sustain per note = detached 0.36/0.14/0.04/0.04/0.00 s vs legato
0.36/0.03/0/0/0 s. First note pays full ignition; the rest ride the
standing wave. Not an envelope trick, not overlap masking. Playable:
renders/dsp/audition/stk_port1/bowed_REF_legato.wav.

MForce implications (for the articulation brainstorm, backlog 68 —
NOT built): the GRAPH is already legato-capable — DelayLine reads its
frequency pin per sample, so a mid-voice frequency step retunes the
loop instantly (KS-bend precedent). What's missing is note DELIVERY:
every score event today gets a fresh voice with prepare() clearing all
state. A slur needs: same voice, no state reset, NoteState frequency
stepped (+ duration re-armed), bow/breath envelope held in sustain
instead of retriggered. That is exactly the Slide/HammerOn/PullOff
family already in the Articulation vocabulary (music/basics.h), the
backlog-53 live mono mode twin, and a concrete Tier-2 case for
backlog 68's per-articulation dispatch in PlayNote. Physical bonus:
ignition cost varying with phrase position is REAL bowed-string
behavior — legato delivery buys realism, not just smoothness.

## CANONICAL PASS, ALL FAMILIES (Matt's directive before leaving 09-13)

- **Clarinet canon: VALIDATED.** 48k biquad refit of the 22050 OneZero
  (zero pinned at 11025, DC=1, band err <=0.075; scipy fit in the notes
  header of gen_stk_clarinet.py), 22050-second gestures,
  duration-adaptive envelope + 0.06 s LoopGate, loop-identity knots
  (total = 0.5/f). Sub-sample f0 measurement (parabolic autocorr —
  added after catching myself calibrating against the integer-lag grid)
  showed the knots exact: **C3..C6 within +-2c of the 22050 reference
  across all 8 variants** (mostly <1c). C7 degenerate in the reference
  itself (5-sample bore).
- **Flute canon: VALIDATED, zero calibration needed.** Pole
  0.6^(22050/48000), DC-block pole likewise, exact-phase knots:
  **C3..C6 within +-1c (noise-off +-0.5c, Hprof <=2 dB)** first try.
  C7 chaotic in the reference.
- **Brass canon: PARTIAL, root cause proven.** Lip radius/b0
  rate-transformed (bandwidth-in-Hz + impulse-invariant peak) locks C5
  at +5c with right character, but the ignition MAP shifts (port C5/C6
  vs ref C4/C5). Cause found and CONFIRMED BY EXPERIMENT: the lip's DC
  gain at 48k is 2.13x the 22050 value (= the rate ratio) even with
  peak preserved — one b0 cannot hold DC and peak simultaneously across
  rates, and the un-blocked mouth-pressure DC sets the ignition window
  timing (C4 SPEAKS with a DC-preserving b0, at reduced level). The
  clean fix is a keytracked DC-matching zero in Biquad resonance mode —
  small engine extension, parked: STK Brass is the weakest STK model
  and the junction-state roadmap supersedes it. soft/hard canon
  variants lock C4/C5 within +-9c (softer breath = survivable window,
  consistent with the DC story).
- Audition restaged: clarinet/flute canon ports vs 22050 refs;
  brass_port_canon added alongside the verbatim pair. Roundtrip 59/59
  across all canon + extension patches.

## BOWED EXTENSIONS ROUND 1 (same day — the campaign's actual payoff)

tools/gen_stk_bowed_ext.py: 13 zero-engine-code cells on the canonical
base, RETUNED TO EQUAL TEMPERAMENT (the port faithfully inherits STK's
own +7/+25/+49c sharpness; extensions stop inheriting it — control cell
measures +0c C3..C5). Gates: f0 +-35c + flutter <0.25 at C3..C5, then
near-dup cull vs control (none culled; min distance 5.6 dB). 12/13
passed -> renders/dsp/audition/stk_bowed_ext1/ + README.

Findings:
- **Hysteresis stick/slip junction IN the proven chassis locks +-7c
  across C3..C5** (3 of 4 cells; c12_d6 killed, C5 silent). The native
  harness never got an in-tune multi-octave lock from this junction —
  the chassis was the missing half. This is the r5c-style axis the
  junction roadmap wanted, now with a validated home.
- Torsional coupling (Mohonk 't', folded): passes, pulls pitch +5..16c
  (physical loading), C6 regime-flips.
- Dispersion allpass (tuning re-comped in knots): clean pass, partial
  positions move (15-19 dB profile shift) at unchanged centroid.
- Bow-bite pressure gesture: pressure is a live pin — a per-note attack
  gesture STK's static CC cannot express. Clean pass.
- Body A/B/ablation (Maestre-48 / 1986-500Hz / none): clean passes, big
  measured differences (centroid +60% / +20%).

## LARSON PROVENANCE + EXTENSIONS ROUND 2 (09-13 night)

Matt: "if the math and code are published, shouldn't we reproduce it
exactly?" Facts established: the Mohonk WAVs are **Peder Larson's Music
421 final project (2003, "Creating a Virtual Cello")** — writeup found
and archived (docs/research/stk_port/larson2003_virtual_cello.pdf) plus
4 more WAVs (bach/bachd cello-suite excerpt, stacatto/stacattod). His
CODE was never published; the writeup gives structure + constants. Not
exactly reproducible: his measured cello-body IR (~2000-tap FIR +
biquads — likely the single biggest reason his WAVs sound like a
cello), bridge filter coefficients, and SKINI gesture data. Everything
else is documented: finite bow width h=2 (his "most significant
improvement"), hyperbolic stick/slip friction (f = mu(v-vb) stick,
2Z(v-vh) slip), torsion impedances Z=0.55/Zt=1.8 kg/s, dispersion
B=4e-4 (cello D). Rate discipline going forward: ports get validated AT
the reference's native rate (patches carry sampleRate), canonicalization
becomes a separately verified transform.

**Brass canon COMPLETED patch-level** (no engine change needed): the
2.17x DC-gain excess is constant across notes, so a fixed low-shelf
before the lip (corner 12.5 Hz, DC 0.461, unity+zero-phase in-band)
fixes every note at once. Ignition map now matches the 22050 reference
(C4/C5 speak, false C6 gone); intonation +29/-8c at C4/C5 = the port's
own lock offset, left for a trim round. Brass is UN-PARKED per Matt
(MForce has no brass at all — that outranks "STK brass is weak").

**Extensions round 2** (tools/gen_stk_bowed_ext2.py, cello register
C2 G2 D3 A3 D4): 5/7 staged to renders/dsp/audition/stk_bowed_ext2/ —
control2 (+0c all slots), width_h2 (+-1c low, -7..-11c top from
finite-width loading), tors_phys (impedance-correct, +2..+9c), disp16
(8-AP fit; D-string range C2..D3 +-12c, detunes above by design — the
filter belongs to a STRING), larson_btd (b+t+d combined, +25c sharp,
retunable). KILLED: both hyperbolic cells — diagnosis in the README
(identity stick = total cancellation; STK's 0.98 cap is a load-bearing
slip leak; plateau discontinuity buzzes) — gets its own round. Body
deferred: public cello IRs listed in the README for Matt's ear-pick,
then a biquad-stack fit.

## LARSON BODY FILTER — RECOVERED FROM HIS PUBLISHED WAVS (09-13 night)

His body filter (unpublished) estimated by LTAS ratio of three
independent with/without-body pairs (bowedbt/bowedbtbody,
bowedt/bowedtbody, bowedbtd/bowedbtbodyd); the three estimates agree to
~4 dB median across 80-5000 Hz, so the ratio is the FILTER, not the
takes. Measured curve: -5 dB @90 Hz, **+20 dB @150 / +14 @200** (main
body resonance block), -10 @500 (notch), **+15 @800 / +11 @1200**
(bridge-hill formant), -22..-30 dB above 2 kHz (steep body rolloff).
ROUND 3 ENTRY POINT: fit a 10-16 section biquad stack to this curve
(recompute at finer resolution from the same pairs), hang it on the
canonical chassis output, A/B vs bachd.wav. General lesson, Matt's
framing: unpublished parameters are an inverse problem — what they
didn't publish, their outputs still constrain, and the
render-measure-compare loop + CMA-ES + ML ears is the recovery
machinery. TABLED 09-13 night; next session = steering-meeting
execution (workflow/cleanup) per Matt.

## Campaign status after day 1

- 4 families ported, all validated against ground truth with machine
  gates; ~35 min wall including engine work.
- Engine additions: Biquad (raw + resonance modes), BowTable. Both
  registry-only; null gate over library+baselines confirms existing
  patches byte-identical (same 2 pre-existing DIFFs as the morning run:
  the uncommitted piano_default/viola_default modifications).
- The steal-first thesis held: port-vs-reference is a taste-free
  objective a session can grind unattended, and every "weird" result so
  far (C6 multiphonic, brass silence) was the REFERENCE's behavior,
  reproduced — not a bug to theorize about.
- Next STK candidates when wanted: Saxofony (blown bowed-string hybrid),
  BlowHole (clarinet + register hole + tonehole), Bowed's Mohonk
  variants (torsion/dispersion/body via the archived JOS examples),
  BandedWG (bar/bowl percussion).

## ROUND 4 — REMAINING FAMILIES (09-15, autonomous)

Saxofony / BlowHole / BandedWG ported and validated at NATIVE 22050
(rate discipline from round 5 of the bowed thread; canonicalization is
a separate later step). Drivers: tools/stk_ref/{saxofony,blowhole,
bandedwg}_ref.cpp; generators: tools/gen_stk_{saxofony,blowhole,
bandedwg}.py; ears queue renders/dsp/audition/stk_port2/ (REVIEW 59).

- SAXOFONY ("blowed string"): bowed-port two-delay skeleton + clarinet
  reed idiom. STK's own delay identity (sr/f - 1.5, position split)
  locks +-1c with NO structural correction. noise_off C4 = 0.04 dB
  exact. Divergent cells (pos_bridge C6/C7, reed_hard, aper_wide) are
  the model's barely-igniting regimes — ref RMS 0.01-0.02 there.
  Positive reed slope (+0.3) = ascending Shaper curve.
- BLOWHOLE (clarinet + register vent + 3-port tonehole): vent =
  current-sample PoleZero (Biquad), tonehole = self-tapped PoleZero
  ({"tap": ThFilt} closes the z^-1). Loop needed a measured comp
  (~0.6 samples, per-note anchors) — the model is register-BISTABLE
  within a ~0.03-sample window at C4/C5 (fixed 9-sample junction
  spacing vs short bores), so noisy variants land in different
  registers than the ref, AND the ref does the same against itself
  (ref-vs-ref: default C4 -702c). noise_off locks -0.3/-3.6/-1.3c.
  C6/C7 out of model range at 22050 (D1 length goes negative).
- BANDEDWG (banded waveguides, Essl & Cook): per mode = raw-Biquad
  zeros (1,0,-1) in series with resonance-mode Biquad (b0 = .5(1-R^2),
  frequency pin = f*mode_k) + integer delay + tap feedback. Loop
  structure exact (single-mode probe rings at 1400.7 Hz matching an
  STK-exact python sim; the ring-at-BP-center regime dies with ONE
  extra loop sample — probe before assuming). Struck: burst must
  enter the DELAY LINE (BP zeros kill DC); decay rates match exactly;
  residual mode-2 level -6 dB = attack detail (instant pre-fill vs
  streamed burst). Bowed: blooms ~4 s (measured on STK itself: energy
  doubles per ~0.5 s, saturates ~4 s) — reference driver gives bowed
  variants 5 s slots; tbar_bowed +-1c. bar_bowed NOT ported: the
  reference itself is silent (no self-oscillation at this pressure).
  f-dependent structure (mode drops, 1/nModes, f<=1568 clamp) baked as
  step-shaped CurveNodes on note frequency.

ENGINE FINDS EN ROUTE (filed): backlog 71 double-advance behind a
shared stateless node in loop context (repro pair in baselines;
minimal case is guarded correctly); 1-sample seconds-mode envelope
stages render SILENT (2-sample stages fire; keep bursts >= 2 samples);
engine BowTable outputs dv*rc — the multiply is folded in, do not
multiply by dv again (that squared error also masked the bloom).
