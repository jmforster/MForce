# Nonlinear bore ("search for brass") — probe round design

2026-09-17. Campaign front greenlit by Matt 09-16; donor = the classic IRCAM
trombone model (Msallam/Tassart lineage). Literature basis:
docs/research/nonlinear_bore/MSALLAM_DIGEST.md (all citations there; this spec
only references it). Steal-first discipline: the model below is the published
one, expressed in existing MForce primitives; nothing is invented.

## What the physics says (sourced, digest §1–2, §6)

- "Brassy" = nonlinear wave steepening in the trombone's CYLINDRICAL slide at
  forte: propagation speed c(u) = c0 + ((γ+1)/2)·u (ICMC'97 Eq. 10), so
  pressure peaks outrun troughs → wavefront steepens → harmonic energy grows
  with level. Absent at piano, dominant at ff (JASA '96 measurements).
- The effect is primarily on the RADIATED sound, not the self-oscillation:
  the HF created by steepening radiates at the bell and is not reflected back,
  so the lip loop barely feels it (JASA '96 conclusion; confirmed in ICMC'97
  Fig. 8 — input wave spectrum ~unchanged). In-loop placement adds only a
  small tuning shift (<5 cents at weak dynamics, secondary sources).
- Chain: lips (nonlinear) → slide (nonlinear propagation + linear losses) →
  bell/radiation (linear low-pass reflection, ~800 Hz bandwidth).

**Consequence for us: the probe is a ONE-WAY, OUT-OF-LOOP steepener on the
output path of existing loop patches.** This is variant 1 of the Acta
Acustica paper, and per the sources it carries the main audible effect. It
also means the steepener is a general-purpose "brassifier" applicable to ANY
patch output — worth knowing even if brass proper needs more.

## Probe implementation — zero engine code

The lumped discrete form (digest §7, Cooper & Abel §4.2, consistent with
Eq. 10 linearized): replace the bore's fixed delay D samples with a
signal-modulated delay

    d(t) = D · (1 − depth · x(t))

where x is the traveling wave (the patch's tapped output, roughly ±1) and
depth is the calibration knob standing in for ((γ+1)/2)·u_peak/c0 (physical
scale ~1–3% at ff over a 2–3 m bore, i.e. Δd up to ~10 samples on D≈380;
sweep wider, ears decide). Positive x (compression) → shorter delay = arrives
earlier. C&A justify the single lumped delay because in-bore pressure is
dominated by low frequencies; they also report that OMITTING shock handling
sounds brighter and more musical — so no shock treatment in the probe.

MForce wiring (all existing nodes):

    loop patch output e ──► DelayLine (source = e)
                              ratio pin ◄── Shaper/Curve( RefSource(e) )
                                            mapping x → base·(1 − depth·x)
                            ──► Biquad bell low-pass (optional axis)
                            ──► mixer

- DelayLine reads fractionally with per-sample modulatable length
  (engine/include/mforce/source/delay_line_source.h) — length =
  sampleRate/frequency·ratio; pin frequency to a constant, drive ratio.
  compensate stays OFF (this is not a tuned loop member; it sits outside).
- Aliasing: linear-interp read at 48k, no oversampling — accepted for the
  probe (C&A call the FIR HF droop "not unwelcome"); note in README.
- The modulated audio delay IS the known approximation (output-time indexing
  vs the faithful input-time formulation) — see Round 2.

## Cells and axes

1. **Dose-response control (the science gate):** pure sine (e.g. 233 Hz ~ Bb3)
   through the steepener. depth = 0 must null against the un-steepened render
   (dead control); depth ladder (e.g. 0, 0.005, 0.01, 0.02, 0.04, 0.08) must
   show monotonic harmonic growth (HF energy / spectral centroid). A sine
   steepening into a sawtooth-like wave = textbook signature.
2. **Level dependence (the brass signature):** same cell at two input gains
   (pp vs ff into the steepener) — effect must appear WITH level, since the
   modulation is proportional to signal amplitude. This is intrinsic to the
   mechanism; demonstrate it, don't assume it.
3. **Musical cells:** 2–3 existing wind loop patches (oboe_default,
   flute_default from patches/library/winds/; optionally the stk brass canon)
   with the steepener on the tapped output: depth ladder × bell-LP on/off.
   Bell LP = Biquad low-pass, corner ~800 Hz–2 kHz axis (the sources give
   only "~800 Hz reflection bandwidth"; radiated sound = high-passed
   complement in real horns — for the probe just try with/without and one
   corner sweep; keep it small).
4. **Sign-flip control (1 cell):** depth negative — steepens the wrong edge;
   should sound different, documents that sign matters.

Machine gates before ears: depth-0 null; dose-response monotonicity;
feature-audibility gate per WORKFLOW.md (REVIEW-64 lesson — the difference
must be audible, not −60 dB trivia); level-ceiling rule. Ears queue ≤ ~8
cells to renders/dsp/audition/nlbore_probe1/ with README (question for Matt:
"does forte get brassy?").

## Round 2 (gated on probe evidence + Matt's ears — NOT this round)

- Faithful Tassart element as a dedicated node: input-indexed delay via the
  d⁺→d⁻ implicit conversion (delay-signal-domain feedback filter, integer p>1
  in the loop, Lagrange order ≤2, g/g⁻¹ maps) — digest §2.3–2.4 has the
  complete recipe. Motivation: the lumped audio-delay modulation is the
  "commonly copied" approximation; the IRCAM structure is the real thing.
- In-loop placement (Acta Acustica variant 2): backward wave steepened inside
  the feedback loop — adds brassiness + small f0 shift.
- Viscothermal loss filter and a measured bell reflection/transmission pair
  (Smyth & Scott 2011 is open-access and has measured trombone bell filters —
  digest §10).
- A proper lip round on the loop family (the digest's S5 one-mass parameter
  set as starting point) if the steepener + existing exciters still read
  "saxy not brassy".
