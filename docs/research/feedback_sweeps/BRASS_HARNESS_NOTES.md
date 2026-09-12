# Brass harness — component notes, reed diffs, hand-tweak guide

2026-09-12, rounds 1/2. Spec: 2026-09-12-brass-harness-design.md.
Tool: tools/gen_brass_harness1.py. Queues: brass_harness1 family.
NO new engine components: the per-sample morph pin IS the 2D junction's
N=2 state input (lip displacement blends closed->open curve pair);
backlog 66's walk fix already keeps the coupling out of the tuning.

## What it is

The one-DOF outward-striking lip valve on a long bore (CCRMA Brasses;
continuation-study structure): lip = keytracked SVF resonator whose
displacement MORPHS the junction between closed-lip and open-lip
curves; bore = delay at MODE x the note period (the played note is
mode n of the bore - a trumpet C5 rides a ~C3 air column); mouthpiece
cup = fixed bandpass; bell = lowpass reflection in the cycle, highpass
transmission as output.

## The four build lessons (each measured)

1. **Static pressure must bypass the cup.** The cup is a bandpass -
   it annihilates DC, and blowing pressure had ZERO effect until it
   entered post-cup at the junction (Drive_sum). The cup then doubles
   as the cycle's DC block.
2. **Brass ignition is a WINDOW, not a threshold.** Over-blowing
   extinguishes (the junction clamp region). Bisection is the wrong
   search; the generator scans pressures linearly and sits at the
   window median. This is real physics (pedal-tone/no-speak regimes).
3. **The lock criterion must be pitch-gated.** RMS alone cannot tell
   cup-centered rustle from a mode-locked note; the probe requires
   energy-at-f0/total > 0.25 (Goertzel).
4. **Mode selection is winnable but register-bound (so far).** Round 1
   locked only near the cup resonance; deepening the lip coupling
   (LipOpen drive 4, cup Q 0.8) bought a 3-octave locked register
   (C3-C5 within -67..+38c at liptune 1.0). C6+ stays unlocked - and
   is beyond real trumpet range anyway. liptune 0.85 column loses the
   low register: lip-at-the-note (1.0) wins in this chassis.

## Differences to the reed/flute harness

| | Reed/flute (wind) | Brass harness |
|---|---|---|
| Valve | single memoryless curve (reed resonance far above note) | curve PAIR morphed by a lip resonator NEAR the note (outward-striking sign in the pair) |
| Bore | delay ratio ~1 (bore = the note) | ratio MODE 2-3 (note = mode n of a long bore) |
| Pressure | Drive_env scales curve window | Mouth env biases the junction operating point, post-cup; window-limited (over-blow chokes) |
| Loop filter | damp keytrack + res honk | bell LP, FIXED cutoff (not a tone-hole instrument) |
| Extra resonance | none | mouthpiece cup, FIXED ~500 Hz, in-cycle (also the DC block) |
| Output | loop + formants | bell HP complement of the bore (radiated highs) |

## Measured state

- Locked register C3-C5 (t100 column); q15_m3 the best overall.
- ML ears vs Iowa trumpet ff: 1.65-1.90 (string harness scores 1.11
  for calibration; the additive-viola lineage started at 1.28).
  Gap terms: harm 1.34-1.55 (spectral balance off) and broadband
  ~2.5 (no turbulence texture yet). Output-blend test (Bore 0.6 under
  the radiated HP): total 1.69->1.65 but harm WORSENED - the balance
  fix is not a simple dry-mix, stopped there per the no-blind-tweaks
  rule. Motion terms are already good (lock jitter reads as brass).

## Hand-tweak guide (mechanism per knob; ears decide)

- **liptune (Lip_curve a):** which mode the lip backs. 1.0 = the
  note; sweeping it live is lip-bend/rip territory; 0.5 should pedal.
- **Lip Q (5-15):** selectivity vs slot-tolerance - high Q locks
  harder but is pickier about liptune; the cracked-note boundary
  lives here.
- **LipOpen drive (4):** coupling depth - the single biggest lever
  found this round (1-octave -> 3-octave lock when raised from 1).
  More = harder gating (buzz); less = breathy non-lock.
- **Mouth maxValue:** the pressure WINDOW (found per config by the
  scan, typically 0.2-0.45 here). Low edge = ignition flutter; high
  edge = choke. Dynamics live inside the window; an envelope through
  it is an articulation.
- **CUP_HZ / CUP_Q (500 / 0.8):** the mouthpiece formant AND the
  competing resonance - raising Q re-creates round 1's cup-lock
  failure; moving CUP_HZ moves the bright band.
- **BELL_HZ (1000):** mute/brightness axis both in-loop (reflection)
  and at the output (transmission) - a continuous mute lever.
- **Curve pair:** the flow model. The committed pair measurably locks;
  the "steep at operating point" redesign lost the lock (git history)
  - move points in SMALL steps and re-check lock per note.
- **Bore amplitude (1.5):** loop-gain trim that made ignition
  possible; also interacts with the pressure window width.
- **Next levers if harm term resists hand-tuning:** cup/bell as a
  measured impedance pair rather than two free filters; the N-station
  curve stack (2D junction spec) if the N=2 pair saturates; the
  modulated-fractional-delay brassiness for the ff sheen.
