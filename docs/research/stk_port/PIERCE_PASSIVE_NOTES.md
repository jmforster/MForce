# Pierce / Van Duyne passive nonlinear filter — structure, provenance, and the
# one place the literature's discrete form has to be tightened

Target design: Pierce, J. R. and Van Duyne, S. A., "A passive nonlinear digital
filter design which facilitates physics-based sound synthesis of highly
nonlinear musical instruments," *JASA* **101**(2), 1120–1126 (1997).

## Provenance — what was actually read

The JASA article itself is paywalled (pubs.aip.org, asa.scitation.org; the
academia.edu mirror 403s). The filter's structure was recovered in full from
two independent primary sources that agree exactly:

1. **US Patent 5,703,313 A**, "Passive nonlinear filter for digital musical
   sound synthesizer and method" (Van Duyne & Pierce, assigned to Stanford) —
   <https://patents.google.com/patent/US5703313A/en>. This is the patent
   covering the JASA design; its FIG. 8 and accompanying description give the
   block diagram, the difference equations, the switching rule, the physical
   derivation (spring stiffness `k` against waveguide impedance `R0`), and the
   energy argument. Quoted below.
2. **Faust `filters.lib`, `fi.apnl`** — written by Julius O. Smith III, who
   cites the JASA paper directly:
   <https://raw.githubusercontent.com/grame-cncm/faustlibraries/master/filters.lib>
   and <https://faustlibraries.grame.fr/libs/filters/>. Source:
   ```faust
   // Passive Nonlinear Allpass based on Pierce switching springs idea.
   // Switch between allpass coefficient `a1` and `a2` at signal zero crossings.
   apnl(a1,a2,x) = nonLinFilter
   with {
      condition = _>0;
      nonLinFilter = (x - _ <: _*(condition*a1 + (1-condition)*a2),_')~_ :> +;
   };
   ```

The route into MForce is Chafe, "Extensions to the 2D Waveguide Mesh for
Modeling Thin Plate Vibrations" (ICSV26, 2019), which names Pierce's
differential-stiffness rule as the source of gong-like modal upwelling at a
mesh boundary; the mesh port already carries Chafe's static allpass as
`edgeMode 1`.

## The structure

A **first-order allpass filter with a time-varying coefficient**, one per
terminating node. The patent: "The PNF 102 is essentially a first-order
allpass filter with a time-varying coefficient." FIG. 8 is two unit delays,
decision logic, one multiplier, two adders.

Difference equations exactly as the patent gives them (`f_r` = incoming wave,
`f_l` = reflected/outgoing wave, `u` = internal state):

```
u(n)   = f_r(n) - a_0(n-1) u(n-1)          (state update)
a_0(n) = a_1  if u(n) <  0
         a_2  if u(n) >= 0                 (decision logic 176)
f_l(n) = a_0(n) u(n) + u(n-1)              (output)
```

Faust's `apnl` is the same recurrence (its `condition = _>0` merely swaps which
name sits on which side of zero).

With `a_0` held fixed this is the ordinary first-order allpass
`H(z) = (a_0 + z^-1) / (1 + a_0 z^-1)` in direct form II — unity magnitude, all
the action in the phase.

## The physics, and therefore the pin semantics

The coefficient is a **spring stiffness** seen through a waveguide of
characteristic impedance `R0`:

```
a_0 = (k - alpha R_0) / (k + alpha R_0)
```

"`a_0` ranges from -1 to 1 as `k` ranges from 0 to infinity." So `a_0 = -1` is a
free (infinitely soft) termination, `a_0 = +1` an infinitely stiff one. The
**nonlinearity is the asymmetry**: a termination whose stiffness differs
between the positive and negative halves of its displacement. Hence the pin
names used in the engine:

* `coefNeg` = the patent's `a_1` — stiffness coefficient while the state is
  negative,
* `coefPos` = the patent's `a_2` — stiffness coefficient while the state is
  zero or positive.

`coefNeg == coefPos` is an exactly linear allpass (verified: bit-identical
output). The distance between them is the drive of the nonlinearity.

## The energy argument, and where it does not survive discretization

The patent's passivity argument is a *timing* argument, not a state
correction. The physical force on the spring is

```
f(n) = [u(n) + u(n-1)] / 2
```

and the stiffness is allowed to change only where that force passes through
zero: "when `u` changes sign between times `n-1` and `n`, the spring
displacement is closest to zero. This is the physically correct time to let the
spring stiffness coefficient change." The energy stored in a spring is a
function of force and stiffness; at zero force the stored energy is zero, so
swapping `k` costs nothing.

**In continuous time that is exact. In discrete time it is not**, because the
sample where the sign flips does not land on the zero crossing — `u(n)` and
`u(n-1)` straddle zero but neither is zero, so a nonzero amount of stored
energy is re-scaled by the stiffness change. Measured here (`float64`, open
loop, cumulative `sum y^2 / sum x^2`, worst case over white-noise bursts,
continuous white noise, a 20 Hz–20 kHz sweep, a full-scale square, an impulse
and DC, across eight `(a_1, a_2)` pairs from `(0.5,-0.5)` to `(0.999,-0.999)`):

| reading of the recurrence | worst cumulative out/in energy |
|---|---|
| patent / Faust literal (state update on `a_0(n-1)`, output on `a_0(n)`) | **2.431** |
| coefficient held constant across the sample, decided from `sign u(n-1)` | **1.475** |
| switch on the sign of the patent's force `u(n)+u(n-1)` instead | **4.755** |
| held coefficient + energy-preserving state rescale (**what MForce ships**) | **1.000000000** |

All three literal readings can *create* energy. The reason is visible in the
per-sample energy balance. Writing `E(n) = (1 - a_0(n)^2) u(n)^2`,

```
f_r(n)^2 - f_l(n)^2 = E(n) - E(n-1) + 2 u(n) u(n-1) [a_0(n-1) - a_0(n)]
```

The last term is nonzero only on a switching sample, where `u(n) u(n-1) < 0`.
Its sign therefore flips between the up-crossing and the down-crossing, so the
two do not cancel over a cycle, and whichever crossing has the larger
`|u(n) u(n-1)|` wins. That is exactly the stored energy the continuous-time
argument assumes away.

## What MForce implements

Same structure, same switching rule, same pins. The coefficient is held for the
whole sample, and the stiffness change is made **exactly** energy-preserving by
carrying the state at constant `E = (1 - a_0^2) u^2` across it:

```
u(n)  = f_r(n) - a u(n-1)
f_l(n) = a u(n) + u(n-1)
a'    = coefNeg if u(n) < 0 else coefPos
if a' != a:
    u(n) <- u(n) * sqrt( (1 - a^2) / (1 - a'^2) )      # same stored energy
    a    <- a'
```

Why this is the paper's filter and not a substitute:

* With `a` fixed over a sample the allpass satisfies
  `f_r^2 - f_l^2 = E(n) - E(n-1)` **identically**, so the sample step is
  lossless by construction.
* The rescale makes the stiffness change lossless *by construction* rather than
  by the assumption that `u ~ 0`. It multiplies a state that is near zero
  precisely where the paper says the energy is negligible, so it is a small
  correction applied exactly where the paper's own argument lives, and the two
  coincide in the limit of a switch landing on the true zero crossing.
* When `|coefNeg| == |coefPos|` — which includes the symmetric `±a` case Faust
  ships as its own example, `apnl(0.5,-0.5)` — the rescale factor is exactly
  **1** and the implementation reduces to the literal recurrence with the
  switching sample's output taken on the outgoing coefficient.
* `coefNeg == coefPos` is bit-identical to the linear first-order allpass.
* The nonlinearity is undiminished: a pure 300 Hz sine in, strongest content
  above 400 Hz out, is **-10.8 dB re fundamental for both** the literal form
  and the shipped form (input itself: -174 dB). Passivity costs nothing in
  spectral spreading — it only removes the energy creation.

Coefficients are clamped to `[-0.999, 0.999]` so `1 - a^2 >= 2e-3` and the
rescale cannot divide by zero. State and coefficient are kept in `double`
inside the filter so that repeated switching cannot accumulate a float32 bias
in a feedback loop; the node interface stays `float`.

Gate: `tools/engine_tests` (`run_pierce_passivity_tests`) drives the shipped
`PierceAllpass` open loop with the six probe signals above across the eight
coefficient pairs and asserts the cumulative energy ratio never exceeds
`1 + 1e-6`.

## Where it is wired

* `engine/include/mforce/core/pierce_allpass.h` — the filter itself.
* `Mesh2D` `edgeMode 2` (`"pierce"`), pins `coefNeg` / `coefPos`, one filter
  per edge node on the `x = 0` and `y = 0` faces — the placement Van Duyne's
  patent describes ("connecting these passive nonlinear filters to 2-D Digital
  Waveguide Mesh boundary terminations"). `edgeMode` 0 and 1 are untouched.
* `PierceFilter` — the same filter as a standalone graph node, so it can
  terminate a 1D delay loop (`engine/include/mforce/source/pierce_filter.h`).

Because the filter is passive, neither use needs a drive ceiling: the
half-drive normalisation and clamp band that `edgeMode 1` needed (see
`tools/gen_mesh2d_ext1.py`, "WHY 0.5 AND NOT 1.0") are not required here.
