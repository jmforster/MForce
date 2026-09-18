# One-mass lip, round 1 — the published brass exciter in the graph

2026-09-17. Spec `docs/superpowers/specs/2026-09-17-onemass-lip-design.md`.
Generator `tools/gen_onemass_lip1.py`. Patches `patches/sweep/onemass_lip1/`.
Ears queue `renders/dsp/audition/onemass_lip1/`. No engine code was touched.

---

## 1. Source transcription — Berjamin et al., arXiv:1511.04247 §3.1

Fetched fresh this run (`curl https://arxiv.org/pdf/1511.04247`, 30 pages,
640 771 bytes) and transcribed from the PDF's own text layer, with §3.1 and
§4.2 re-extracted in **layout mode** so the Table-2 column alignment the
digest flagged as uncertain could be resolved. Equations 43–49, verbatim in
the paper's notation:

```
(43)   A = h l sin(phi)

(44)   pe(y,t) = p+_e(y,t) + p-_e(y,t)
               = p+(0,t)   + p-(0,t)

(45a)  m y'' + r y' + k (y - yeq) = f(y,t)
(45b)  y(0) = y0 ,  y'(0) = y1

(46)   f(y,t) = A ( pm(t) - pe(y,t) )

(47)   pe(y,t) =
         2 p-_e - xi (psi y / 2) ( psi y - sqrt( psi^2 y^2
                                   + 4 |pm - 2 p-_e| ) )      if y > 0
         2 p-_e                                               else

(48)   xi(y,t) = sgn( pm(t) - pe(y,t) ) = sgn( pm(t) - 2 p-_e(y,t) )

(49)   psi = l Zc sqrt(2/rho0) = l sqrt(2 rho0) a0 / S(0)
```

Prose attached to them (paraphrased): only the top lip moves, the bottom lip
is static; the lip is a thin rigid rectangular plate of height `h` and width
`l` at angle `phi` to the x-axis; a spring `k` and damper `r` sit over a mass
`m`. The flow is stationary, incompressible, laminar and inviscid in the
mouth and under the lip, so Bernoulli plus conservation of mass apply; the
sudden cross-section change behind the lip makes a turbulent jet that
dissipates all its kinetic energy with no pressure recovery in the
mouthpiece, and that closes the system for `pe` — Eq. 47.

**Table 2 (§4.2), column alignment resolved from the layout extraction:**

| m (kg) | k (N/m) | r (N.s/m) |
|---|---|---|
| 1.78·10⁻⁴ | 1278.8 | √(m k)/4 |

| l (m) | A (m²) | pm (Pa) |
|---|---|---|
| 10⁻² | 10⁻⁴ | 20·10³ |

| y0 (m) | y1 (m/s) | yeq (m) |
|---|---|---|
| 4·10⁻³ | −4 | 5·10⁻⁴ |

The digest's provisional y-row (`y0 = 4e-3 m, y1 = -4 m/s, yeq = 5e-4 m`) is
**confirmed correct** — the uncertainty flag can be cleared. Two further
numbers were taken from the same section: the resonator is a constant-radius
`R = 7 mm`, `D = 1.4 m` cylinder on `Nx = 100` points, and their parameters
come "both from different publications and from trial and errors until
self-oscillations are obtained". They sweep `k` between 3000 and 100 N/m for
register. Air constants used (their Table 1, 15 °C): `gamma = 1.403`,
`p0 = 1e5 Pa`, `rho0 = 1.177 kg/m³`.

Not transcribed because this round does not use it: §3.2's Newmark
predictor-corrector (Eqs. 50–55) and the fixed-point iteration (Eqs. 53–54),
which is their solution to the one implicit coupling — see §3 below.

**One derived fact that turned out to matter.** `r = √(mk)/4` means
`Q = √(mk)/r = 4` exactly, independent of `k`. So Berjamin's own register
sweep (k from 100 to 3000 N/m) is a **constant-Q sweep**. That is what
licenses keytracking `f_lip` at fixed Q in §3 — the register keytrack sits on
their evidence rather than on my taste.

---

## 2. Pressure ↔ signal scaling (physical-units discipline)

Two reference scales, and nothing else in the generator is a free factor.
Every constant in the file is tagged `PHYSICAL` (SI, from the tables) or
`NORMALIZED` (MForce signal units).

```
P_REF = 20e3 Pa      p_hat = p / P_REF     (Table 2's own pm becomes 1.0)
Y_REF = 1e-3 m       y_hat = y / Y_REF     (1 mm of opening becomes 1.0)
```

Derived normalized constants, each a closed form of the SI table — no fitting:

| symbol | closed form | value |
|---|---|---|
| `a0` | √(γ p0 / ρ0) | 345.255 m/s |
| `S(0)` | π R², R = 7 mm | 1.5394e−4 m² |
| `Zc` | ρ0 a0 / S(0) | 2.6398e6 Pa·s/m³ |
| `psi` | l Zc √(2/ρ0), Eq. 49 | 3.4411e4 Pa^0.5/m |
| `PSI_HAT` | psi · Y_REF / √P_REF | 0.243323 |
| `YEQ_HAT` | yeq / Y_REF | 0.5 |
| `F_REF` | √(k/m)/2π from Table 2 | 426.59 Hz |
| `B0` | A · P_REF / (m · SR² · Y_REF) | 0.004876717 |

`B0` is the `y''` discretization, not a knob. With `y'' ≈ SR²(y_n − 2y_{n−1}
+ y_{n−2})`, a two-pole section whose poles carry the spring and damper
(`a1 = −2 r_p cos ω`, `a2 = r_p²`) realizes `m y'' + r y' + k y = F` when its
input gain is `1/(m SR²)` in SI — i.e. `A·P_REF/(m·SR²·Y_REF)` after
normalization. It is **independent of f_lip**, which is why a fixed `b0`
*setting* on the Biquad is sufficient and no gain keytrack is needed. (The
alternative calibration — matching the section's exact DC gain to the
physical compliance `1/k` — gives 0.004918 at f_lip = 426.6 Hz, 0.8% away;
that 0.8% is the two-pole discretization error, and it is the only place the
digital lip departs from the continuous one apart from the lag in §3.)

Sanity check of the normalization: at y = 1 mm, `(psi·y)² = 1184 Pa` against
`4|Δp| ≈ 8e4 Pa` at Table-2 pressures — a 1.5% correction, so the full Eq. 47
and the plain Bernoulli form agree to ~1% in the playing regime, as they
should. Everything in the graph stays inside ±2 in signal units.

### Three keytracks, which are mine, not the paper's

The paper has one sustained note. C3–C5 needed three decisions, stated plainly:

1. **`f_lip = RATIO · f0`**, so `k = m(2π f_lip)²` follows. At C3 that is
   k = 120 N/m and at C5 k = 1924 N/m — inside Berjamin's own swept
   100–3000 band, so the register stays on their evidence.
2. **Pole radius `r_p = exp(−π f_lip/(Q·SR)) ≈ 1 − π f_lip/(Q·SR)`.**
   Linearized because over 130–525 Hz the exponential's curvature costs
   < 1e−5 in radius, and a linear expression is directly wireable as a
   CurveNode expression knot. Q stays at Table 2's 4.
3. **`pm = S · PM_TAB · (f_lip/F_REF)²`.** The static opening is `A·pm/k` and
   `k ∝ f_lip²`, so this holds the embouchure at one operating point across
   the register; at `f_lip = F_REF, S = 1` it is exactly Table 2's 20 kPa.
   Without it C3 blows the lip 16× too far open and nothing speaks. `S` is
   the dynamics axis, and soft/loud are two points inside the *measured*
   ignition window, reported back in Pa.

---

## 3. The one approximation, and the wall that is not one

Eq. 47 is **explicit**: given `y` (this sample) and `p-` (this sample, off
the bore tap), it returns `pe` with no iteration. So the Bernoulli square
root **ships** — it is a `CurveNode` expression knot, `form: "power", a: 1,
b: 0.5`, applied to `q² + 4|Δp|` which is non-negative by construction. No
linearized substitute is in this build and none was needed.

The only implicit coupling left is Eq. 46: `f` needs `pe`, `pe` needs `y`.
Berjamin closes it with a fixed-point solve to a relative tolerance of 1e−13
(their Eqs. 53–54). A pull graph cannot iterate, so this port breaks the
cycle with a **one-sample lag**: the lip force reads `pe` through a
`{"tap": "Pe"}` edge. That is a standard explicit discretization of the same
system and it is the single approximation in the exciter.

Things that did *not* turn into walls, for the record: the two
signal×signal multiplies are `CombinedSource` Multiply nodes; the `y > 0`
clamp, the equilibrium offset and the `psi` scale collapse into one
three-knot `CurveNode`; `sgn` and `|·|` are three- and four-knot
`CurveNode`s; and every scalar trim rides a `gainAdj` **setting** rather than
an extra node, which keeps the cycle at 12 members against the DelayLine
compensation walk's cap of 16.

### Gate 0 — the wiring is exact

The flow subgraph was rendered open-loop with `y` and `p-` pinned to
constants and checked against `pe_closed_form()` in Python:

| y (mm) | p⁻ | pm | Eq. 47 says | rendered | error |
|---|---|---|---|---|---|
| 0.00 | 0.00 | 1.00 | +0.000000 | +0.000000 | 0.000% |
| 1.00 | 0.00 | 1.00 | +0.215514 | +0.215454 | 0.028% |
| 1.00 | 0.20 | 1.00 | +0.561185 | +0.561157 | 0.005% |
| 0.50 | −0.10 | 0.40 | −0.112872 | −0.112793 | 0.070% |
| −0.60 | 0.10 | 1.00 | +0.200000 | +0.199951 | 0.024% |
| 1.00 | 0.60 | 0.50 | +1.023883 | +1.023804 | 0.008% |

Agreement to int16 quantization, including the closed-valve branch (row 5,
`y < 0` → `pe = 2p-`) and the flow-reversal branch (row 6, `ξ = −1`). The
node-ordering discipline the graph depends on — a twice-referenced node hands
the raw pointer to its *first* mention in wiring order and read-only views
after that, so the raw consumer must also be the one the pull reaches first —
is validated by this gate passing.

---

## 4. The bore had to grow — the one design change I made against the spec

The spec said "loop-family bore". My first assembly used the family's
default: a delay one note-period long. It **ignited immediately and hard**,
and it played the wrong note. Probe sweep at C4, lip ratio 0.6 → 1.4, one
blowing pressure:

| f_lip/f0 | f_lip (Hz) | played (Hz) | played / f_lip | vs the note |
|---|---|---|---|---|
| 0.6 | 157.0 | — | — | dead |
| 0.7 | 183.1 | — | — | dead |
| 0.8 | 209.3 | 271.7 | 1.298 | +65 c |
| 0.9 | 235.5 | 281.9 | 1.197 | +129 c |
| 1.0 | 261.6 | 298.7 | 1.142 | +229 c |
| 1.1 | 287.8 | 316.5 | 1.100 | +330 c |
| 1.2 | 314.0 | 333.1 | 1.061 | +418 c |
| 1.4 | 366.3 | — | — | dead |

Two things in that table. First, **the played frequency is always above the
lip resonance** — that is the textbook outward-striking-valve signature, and
it is the behaviour no memoryless curve can produce. Second, `played/f_lip`
falls from 1.30 to 1.06 as `f_lip` sweeps up through the bore resonance:
the bore *is* pulling, it just loses.

Why it loses is measurable. Differentiating Eq. 47 at the operating point,
the reflection the returning wave sees at the lip end is
`d p⁺/d p⁻ = 1 − 2q/√(q² + 4Δ)`, which at C4 and Table-2 pressures is
**0.38** — the valve absorbs ~60% of the returning wave every bounce. A
one-period loop simply has no authority against that.

Lengthening the bore to **three note-periods** fixes it, and is the real
instrument (BRASS_HARNESS_NOTES delta 2: a trumpet C5 rides a ~C3 air
column). A three-period delay's comb peaks are three times narrower in
absolute time, so the mode holds the lip. Measured at lip ratio 0.8:

| bore length | lip 0.8 | lip 1.0 | lip 1.2 |
|---|---|---|---|
| 1 note-period | +65 c | +229 c | +418 c |
| 2 note-periods | +36 c | +176 c | +731 c (mode 3) |
| 3 note-periods | **+25 c** | +139 c | +550 c (mode 4) |

and the "mode 3 / mode 4" annotations are the point: at the longer bores the
lip is not being dragged, it is **choosing which partial of the air column
speaks**. That is the brass register mechanism, working, in existing nodes.

This is the one place the shipped round differs from the spec's letter, and
it is a bore change, not a lip change — the exciter is the paper's.

---

## 5. Results

### Ignition and lock — the headline

Linear pressure scan (22 steps, S = 0.2…2.3), all three notes rendered at
every step, counting notes that land within ±40 cents with >15% of their
energy at f0. Digits below are "notes locked" per scan step, low pressure
to high:

| config | notes locked per step | window |
|---|---|---|
| `lip_r080` | `0001223333333333333333` | S 0.8–2.3 = **16–46 kPa**, 3/3 notes |
| `lip_r100` | `0000000000000000000000` | never on the written note |
| `lip_r120` | `0000000000000000000000` | never on the written note |
| `flat_r080` | `0000000000000000000000` | never makes a note at all |
| `reed_r080` | `0000000021110000000001` | S 1.0 only = 20 kPa, 2/3 notes |

**The dynamic lip ignites and locks C3–C5 over a 16–46 kPa window** — a
wide, musically usable pressure range that contains Berjamin's own 20 kPa.
Intonation at the in-tune setting:

| cell | C3 | C4 | C5 |
|---|---|---|---|
| `lip_r080_loud_st1` | +18 c | +23 c | +33 c |
| `lip_r080_soft_st1` | +18 c | +22 c | +30 c |
| `reed_r080_loud_st1` | −8 c | −1 c | doesn't speak |

All lip cells inside the ±40 c gate, consistently sharp and widening with
register — a systematic bias, not noise, and exactly the shape you would
expect from a valve that sounds above its own resonance. It is trimmable by
moving the ratio (the sweep says ~0.77 centres it); left alone this round
because the spec said timbre first.

### Register selection — the thing the old valve cannot do

Which partial of the air column actually spoke (bore fundamental = note / 3,
so 3.00 = the written note):

| cell | C3 | C4 | C5 |
|---|---|---|---|
| `lip_r080_loud_st1` | 3.03 | 3.04 | 3.06 |
| `lip_r100_loud_st1` | 3.31 | 3.28 | 3.22 |
| `lip_r120_loud_st1` | 4.27 | 4.13 | 4.12 |

Same patch, same written notes, lip tuning the only change: mode 3 → the
cracked-note zone between partials → mode 4. A memoryless valve has no
handle here at all; on the same long bore it can only ever sound the bore
fundamental (measured: 87.5 Hz against a written C4, i.e. partial 1.00).

### Brightness against blowing pressure

| config | quiet | loud | centroid | energy >1 kHz |
|---|---|---|---|---|
| `lip_r080` | 22.0 kPa | 41.5 kPa | 443 → 500 Hz | 0.053 → 0.111 (**2.1×**) |
| `lip_r100` | 22.0 kPa | 41.5 kPa | 336 → 402 Hz | 0.004 → 0.029 (7.3×) |
| `lip_r120` | 22.0 kPa | 41.5 kPa | 517 → 773 Hz | 0.074 → 0.298 (4.0×) |

Dose-response present on every dynamic-lip config, at matched playback
level. The steepener adds on top of it: at the in-tune cell, brightener off
→ on moves energy above 1 kHz from 0.043 to 0.111 (2.6×) with centroid
451 → 500 Hz, so the two brightening mechanisms — the valve's own harder
closure and the bore's wave steepening — are both live and independent.

### The two controls

- **`flat`** (mass and damper deleted, everything else identical): never
  oscillates, at any pressure in the scan. Sustained RMS 0.009 against the
  lip's 0.119 at the same trim — 22 dB down, and it is residue, not a note.
  A blown-open valve with no memory has no phase to return; **the mass is
  the oscillator**. This is the cleanest possible answer to "did dynamics
  matter?".
- **`reed`** (memoryless inward-striking valve, bore tuned to the note):
  locks C3 (−8 c) and C4 (−1 c), refuses C5, and its ignition window is
  **one scan step wide** against the lip's sixteen — so it has no soft/loud
  pair to give and there is no `reed_soft` file.

### Gates

| gate | result |
|---|---|
| Eq. 47 wiring vs closed form | pass, ≤0.07% (int16 quantization) |
| dead-lip null (S = 0 must not ring) | pass, sustained rms 0.000144 vs 0.004 threshold |
| ignition C3–C5, pitch-gated linear scan | pass for the dynamic lip |
| lock ±40 c | pass, +18…+33 c |
| brightness dose-response on pressure | pass, 2.1× energy >1 kHz |
| feature audibility vs control | pass, +3.0 dB vs reed, +2.8 dB vs flat |
| level ceiling (0.5 s rms ≤ 0.5) | pass, worst 0.377 |
| peak normalization −6 dBFS | pass; `flat` inherits its twin's gain, `reed` RMS-matched to its A/B partner |
| ears budget ≤ 8 | 7 cells |

---

## 6. Ears queue and the question

`renders/dsp/audition/onemass_lip1/`, 7 cells, README in the folder.

| cell | what |
|---|---|
| `lip_r080_loud_st1` | the lip, in tune, loud, brightener on — **A** |
| `reed_r080_loud_st1` | the memoryless valve, same notes, RMS-matched — **B** |
| `lip_r080_loud_st0` | A with the brightener off |
| `lip_r080_soft_st1` | A blown gently |
| `lip_r100_loud_st1` | lip tuned up — the cracked-note zone |
| `lip_r120_loud_st1` | lip tuned further up — the next partial |
| `flat_r080_loud_st1` | A with the mass deleted (near-silence, on purpose) |

**Best A/B: `lip_r080_loud_st1` vs `reed_r080_loud_st1`** — same three notes,
matched RMS and peak under the ceiling, same brightener, same flow
equations, same bore family. The only difference is whether the valve has
mass. THE QUESTION for Matt: does the lip with mass read as brass where the
curve read as a sax?

---

## 7. What is not in this build

- **The mode-selection intonation trim.** All lip cells sit +18…+33 cents
  sharp. A ratio near 0.77 centres it; a per-register ratio curve would
  flatten the widening. Deliberately deferred — timbre round first.
- **The bore is a lumped loop, not a waveguide.** One delay carries the
  round trip, so `p⁻` is the round-trip-filtered `p⁺` rather than an
  independently-propagated counter-wave. Berjamin's own nonlinear
  propagation lives in the slide between the two; here the nonlinear
  propagation is the out-of-loop steepener only (Acta Acustica variant 1,
  per the digest §1.3).
- **No viscothermal loss filter.** The in-loop lowpass at 1200 Hz stands in
  for bore losses plus the bell's reflection, as one filter. The
  literature's √ω wall loss is not modelled.
- **The bell is a plain lowpass on the output**, per the spec and ICMC'97
  Fig. 7, not the reflection/transmission complement the brass harness uses.
- **No LipSource node**, per the spec. Nothing in this round needed one:
  the model wired in existing primitives with a single one-sample lag. If a
  node is ever built, the case for it is efficiency and the fixed-point
  solve, not expressiveness — the graph could already say everything the
  equations do.
