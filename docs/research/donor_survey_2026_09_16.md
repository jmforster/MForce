# Donor survey — Matt's two REVIEW-59 papers (backlog 73)

Read + verdict per paper, per the steal-first discipline. 2026-09-16.

## 1. Krach/Petrausch/Rabenstein, "Digital Sound Synthesis of Brass
##    Instruments by Physical Modeling" (DAFx'04, P_101)

**Matt's question: anything here that beats STK and gets us to brass? NO.**

What it is: Functional Transformation Method (FTM) — the air column
(cylinder + Bessel-horn flare, Webster's horn equation) is solved
analytically into a bank of parallel complex 1st-order resonators
(modal synthesis with physically-derived mode frequencies/decays),
driven through a lumped 2nd-order mouthpiece network (cup compliance
C = Vb/(rho0*c^2), constriction inertance L = rho0*lc/Sc, loss R) and a
one-mass upward-striking lip valve (Fletcher / Vergez-Rodet class:
m*x'' + r*x' + k*x = gamma*(Ps - p), flow u = l*sqrt(2/rho0)*x*sqrt(Ps-p)
gated on x>0 and Ps>p). Delay-free loop eliminated algebraically.

Why it doesn't move us: **the bore is LINEAR** — exactly the ceiling we
named on 09-13 ("brassy" needs nonlinear bore propagation / wave
steepening at forte; linear bore + valve = saxy not brassy). Worse than
that, the damping is frequency-INDEPENDENT (the authors themselves call
it a severe simplification) and the bell radiation load is set to ZERO
(pressure release, no reflection filter shaping). The only nonlinearity
is the lip valve, which is the same pressure-controlled-valve class as
STK's lip and our native lip experiments. Their own results section
concedes "conformity in basic matters ... need for improvement in order
to achieve a more realistic sound." MATLAB, no code published.

Worth stealing anyway (small, cheap, not brass-critical):
- **The lumped mouthpiece two-pole** (C/L/R from cup volume +
  constriction geometry) is a concrete, physically-parameterized
  "mouthpiece formant" our reed/lip patches lack — expressible today as
  a Biquad resonance section in front of the junction. Candidate cell
  axis for any future lip round, zero engine code.
- The Bessel-horn eigenvalue recipe (eq. 34) computes horn mode
  frequencies from geometry (rc, rh, epsilon, xs, xb1, xb2) — a
  "resonator bank from bore geometry" tool if we ever want bodies
  derived from shapes instead of measured LTAS. Parked.

Brass path remains the 09-13 conclusion: nonlinear bore donors
(OpenWind / MoReeSC / NESS lineage) or the native amplitude-dependent
waveshaping surrogate along the bore.

## 2. Chafe, "Extensions to the 2D Waveguide Mesh for Modeling Thin
##    Plate Vibrations" (ICSV26, 2019)

**Matt's question: enough info to produce a harness? YES — fully.**

What it is: STK's own `Mesh2D` (rectilinear 2D waveguide mesh, in the
checkout we already have at ../stk) + three published extensions,
validated against a real struck brass plate (68.5 x 17.8 x 0.4 cm,
c_brass = 1333.5 m/s measured):
1. **Geometry from physics**: mesh size from Eq. 1, N = SR*(len/c) —
   the real plate maps to a 25x6 mesh at 48 kHz. Multi-node outputs
   along a line reproduce the measured wavefront timing.
2. **Edge allpasses for stiffness**: one 2nd-order allpass per edge
   node on two edges (left + bottom; others pure reflections),
   a1 = -2R*cos(wc*T), a2 = R^2, fc = 1575 Hz — mode detuning/ratio
   stretching, "complex metallic timbres."
3. **Signal-dependent allpass = passive nonlinearity** (the real find):
   - r(n) = 0.75 + s*x(n), s = 0.2 -> "bashed aluminum pie pan,"
     parallel time-varying mode tracks.
   - Pierce differential stiffness: s = -0.5 for x<=0, +0.003 for x>0
     -> gong-like modal upwelling (frequency components GROW while the
     tone decays). This is a genuine cheap nonlinear mode-coupling
     mechanism, the ingredient class FDTD gong papers pay dearly for.
4. Cost: 25x6 mesh at 48 kHz ran 5x realtime on a 2015 laptop — cheap
   by our standards. Open-source C++ at cm-gitlab.stanford.edu/cc/
   waveguideMesh (existence unverified; STK Mesh2D suffices for the
   reference driver regardless).

Harness plan (standard steal-first pipeline, slots straight into the
BandedWG percussion direction):
- Stage A: tools/stk_ref/mesh2d_ref.cpp (STK Mesh2D, strike + output
  taps), ground truth at native rate; port the mesh as an engine node
  (Mesh2D is a fixed-topology state machine — same class of port as
  BandedWG); validate wavefront timing + mode set per the paper's
  measurements.
- Stage B: the three extension cells (edge allpass fc/R sweep, pie-pan
  dynamic R, Pierce gong) — all constants published above.
- Zero-code probe FIRST (before any engine work): Pierce's
  sign-dependent stiffness is a 1D idea (he proposed it for a STRING
  bridge termination). Our Biquad resonance mode has frequency/radius
  PINS — wire the loop signal through a Curve into the radius pin of a
  termination filter on an existing KS/loop patch and sweep the +/-
  asymmetry. If modal upwelling shows up in 1D for free, the 2D port
  earns its round with evidence.

Verdict: paper 1 NO-GO as brass donor (small mouthpiece steal noted);
paper 2 GO — feasible, cheap, on-axis with the BandedWG percussion
campaign, with a zero-code 1D probe available before any engine work.
