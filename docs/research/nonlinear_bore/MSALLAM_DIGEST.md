# Msallam / Tassart trombone nonlinear-propagation model — literature digest

Prepared 2026-09-17. Purpose: recover the classic real-time nonlinear-bore brass model
(IRCAM, 1996–2000) in enough detail to port it.

Everything below is tagged:

- **(A)** verbatim-sourced fact, from a document fetched and read in this session, with citation.
- **(B)** standard background I am supplying (textbook / adjacent literature), explicitly labelled.
- **(C)** open question — something the reachable texts do not specify.

Mathematical formulae are transcribed from the sources' own notation. Prose is paraphrased
rather than quoted (copyright); equation numbers refer to the source's own numbering so any
claim here can be checked against the cited section.

---

## 0. PROVENANCE — what I actually read

### Fully read (complete text in hand)

| # | Document | Access | URL |
|---|---|---|---|
| S1 | **Tassart, Msallam, Depalle, Dequidt — "A fractional delay application: time-varying propagation speed in waveguides", Proc. ICMC'97, Thessaloniki, pp. 256–259** | FULL TEXT. Author's own LaTeX2HTML copy (sections `node1`–`node7`). Equations are GIF images; I downloaded all 43 image files, upscaled and read them, so **every numbered equation in this paper is in hand**. | `http://recherche.ircam.fr/anasyn/tassart/these/icmc97/article.html` (http only — https handshake fails; fetched with curl) |
| S2 | **Hirschberg, Gilbert, Msallam, Wijnands — "Shock waves in trombones", J. Acoust. Soc. Am. 99(3):1754–1758 (1996)**, DOI 10.1121/1.414698 | FULL TEXT, publisher version of record, from the TU Eindhoven repository. | `https://pure.tue.nl/ws/files/1503964/617406.pdf` |
| S3 | **Msallam, Dequidt, Tassart, Caussé — "Physical Model of the Trombone including nonlinear propagation effects", ISMA'97 — AUTHORS' OWN ABSTRACT PAGE** | FULL ABSTRACT only (the proceedings paper itself is not online). This abstract is unusually detailed and is the single best primary statement of the *model structure*. | `http://recherche.ircam.fr/anasyn/tassart/these/isma97/abstract.html` |
| S4 | **Cooper & Abel — "Digital simulation of 'brassiness' and amplitude-dependent propagation speed in wind instruments", Proc. DAFx-10, Graz (2010)** | FULL TEXT PDF. **SECONDARY** — not by the Msallam group, but it is the clearest published statement of the amplitude-dependent-delay discretisation, and it cites the Acta Acustica paper for one specific finding. | `https://www.dafx.de/paper-archive/2010/DAFx10/CooperAbel_DAFx10_P77.pdf` |
| S5 | **Berjamin, Lombard, Vergez, Bergeot — "Time-domain numerical modeling of brass instruments including nonlinear wave propagation, viscothermal losses, and lips vibration"**, arXiv:1511.04247 | FULL TEXT PDF, read selectively (§2 resonator, §3 exciter, §4 config, §5 results). **SECONDARY / Tier-B background** — supplies the Menguy–Gilbert equations, air constants and a one-mass lip parameter set, and one statement about the Msallam result. | `https://arxiv.org/pdf/1511.04247` |
| S6 | **IRCAM BASIRcam bibliographic records** for Msallam (6 records: art. 48, 62, 253, 254, 258, 341) | FULL RECORDS incl. verbatim abstracts. Metadata-authoritative (IRCAM's own database). | `http://articles.ircam.fr/index.php?Action=Results&Auteur=Msallam` |
| S7 | **HAL record hal-01105577** (the Acta Acustica 2000 paper) | Record read via the Browser pane (HAL is behind an Anubis proof-of-work wall; plain fetch returns 403). Record states **"No file"**, 242 views / **0 downloads** — i.e. metadata-only deposit. Abstract obtained. | `https://hal.science/hal-01105577` |

### Partially read

| # | Document | What I got |
|---|---|---|
| S8 | Tassart — ISMA'97 presentation slides, `isma97/presentation_slide.ps.gz` | Downloaded and decompressed (5 pages, dvips/TeX bitmap fonts). Only fragmentary word-level text is recoverable; all mathematics is bitmap. **Not usable as a source.** Slide titles recovered: "one problem and one solution", "Forward and backward delay", "two time scales", "Graphical Interpretation" — i.e. it duplicates S1 §3. |

### FAILED to obtain — stated plainly

1. **PRIMARY TARGET #1 — Msallam, Dequidt, Caussé, Tassart, "Physical Model of the Trombone
   Including Nonlinear Effects. Application to the Sound Synthesis of Loud Tones",
   Acta Acustica united with Acustica 86(4):725–736 (2000): FULL TEXT NOT OBTAINED.**
   - HAL deposit `hal-01105577` carries no file (confirmed in-session).
   - IRCAM's own full-text mirror does not have it: `http://articles.ircam.fr/textes/Msallam00a/`,
     `.../Msallam00b/`, `.../Msallam98a/`, `.../Msallam97a/`, `.../Msallam97b/` **all return 404**
     (by contrast `.../Hirschberg95b/` returns 200 — so the mirror works, the paper just isn't there).
   - ResearchGate offers only "Request PDF"; publisher access is paywalled.
   - **What I do have from it: the abstract, verbatim, corroborated across two independent
     databases (HAL S7 and IRCAM BASIRcam record 258 / 253, S6).** Nothing else.
   - **Consequence: no equation, coefficient, filter form or numeric parameter in this digest
     comes from the Acta Acustica paper.** Everything attributed to "the model" comes from the
     1996 JASA companion (S2), the ISMA'97 abstract (S3) or the ICMC'97 algorithm paper (S1).
2. **PRIMARY TARGET #2 — the ISMA'97 proceedings paper itself** (Msallam, Dequidt, Tassart,
   Caussé): not online anywhere I could reach. Only the authors' abstract page (S3) and the
   slides (S8).
3. **Msallam PhD thesis (1998, IRCAM / Paris 6)**, "Modèles et simulations numériques de
   l'acoustique non-linéaire dans les conduits — Application à l'étude des effets non-linéaires
   dans le trombone et à la synthèse sonore par modèle physique", dir. R. Caussé:
   catalogued (BASIRcam record 341) but no online copy found.
4. **Msallam, Dequidt, Dubois, Caussé — CFA'97 (Marseille)** and **Msallam & Dubois,
   J. Computational Acoustics (2000)**: metadata + abstracts only (S6).
5. **Berners, "Acoustics and Signal Processing Techniques for Physical Modeling of Brass
   Instruments" (Stanford PhD, 1999)**: `ccrma.stanford.edu/~dpberner/` returns 404; not
   fetched, therefore not used.
6. **Gilbert, Menguy, Campbell, "A simulation tool for brassiness studies", JASA 123(4) (2008)**
   — the source of the modern brassiness parameter: paywalled, not fetched, therefore
   **the brassiness metric is NOT covered in this digest.**

### Bibliographic correction (A)

The task assumed an ICMC ~1997 paper "by Msallam et al." The real-time implementation paper is
ICMC'97 but is **first-authored by Tassart** (Tassart, Msallam, Depalle, Dequidt) and is a pure
DSP paper about the delay algorithm. The companion *trombone* paper of the same year is
**ISMA'97**, first-authored by Msallam. Note also that the venue of the ISMA'97 paper is recorded
inconsistently: IRCAM's database (S6, record 62) says Singapore; the ICMC'97 reference list
(S1 §References, ref. 5) says Edinburgh, Scotland, August 1997. The authors' own abstract page
(S3) is headed ISMA'97 with no city.

---

## 1. Overall model structure

### 1.1 The blueprint, from the 1996 JASA paper (A — S2)

S2 is the experimental paper that motivated the whole model, and its closing paragraph is
effectively the specification the later synthesis papers implement. Its structural claims:

- **Where the nonlinearity lives.** The nonlinear effect is essential for the transfer of sound
  from source to listener, but can be ignored when modelling the *generation* of the pipe
  oscillation (S2, abstract and closing section). Justification given: the high frequencies
  created by steepening are radiated efficiently at the horn and are *not* reflected at the pipe
  termination, so they do not contribute to regeneration of the lip oscillation.
- **Proposed chain** (S2, penultimate section):
  1. a **linear** pipe model retaining **at least the first eight resonances** determines the lip
     oscillation;
  2. the resulting mouthpiece pressure drives a **simple wave into a cylindrical bore**;
  3. **weak shock theory** predicts the shock, if relevant;
  4. the pressure signal at the end of the cylindrical bore is radiated by a **linear model of the
     horn**;
  5. filtering keeps the relevant audio band and avoids numerical problems with the shocks.
- **Why trombone/trumpet and not saxhorn** (S2): the bright instruments have a *cylindrical* pipe
  segment immediately downstream of the mouthpiece; a conical bore makes the wave decay faster,
  which reduces nonlinear steepening.
- **Why the source matters** (S2): the shock-formation distance depends on the *maximum pressure
  rise rate* in the mouthpiece, which is created by the lips taking over flow control when they
  close; so the primary (source) nonlinearity is what enables the propagation nonlinearity.

### 1.2 The trombone model as the authors describe it (A — S3, ISMA'97 abstract)

Paraphrasing the authors' own abstract, point by point:

- Nonlinear propagation is taken into account **only inside the slide** — i.e. the uniform
  (cylindrical) cross-section part of the horn.
- **Nonlinear interactions between forward and backward waves are neglected**; each travelling
  wave is distorted independently.
- **Linear damping (viscothermal losses) is included.**
- For sound synthesis the **method of characteristics** propagates the waves from one end of the
  slide to the other, treating them as **simple plane waves** (nonlinear travelling, frictionless).
- This method **can also include variation of the slide length**, and is formulated as a
  **time-varying fractional-delay digital filter**.
- A **special treatment based on weak shock theory** is applied when shock waves occur.
- **Viscothermal losses, propagation in the bell, and radiation are all treated linearly.**
- The **lips are a simple one-mass or two-mass model.**

So the decomposition is: `lips (nonlinear exciter) → cylindrical slide (NONLINEAR propagation +
linear losses, bidirectional) → bell + radiation (LINEAR)`.

### 1.3 The two variants in the Acta Acustica paper (A — abstract only, S6/S7)

The 2000 journal paper's abstract states that **two implementations** of the nonlinear
propagation are considered and compared:

1. one where the nonlinear effects **affect only the radiation of sound** (i.e. the nonlinearity
   sits outside the oscillating feedback loop — the "S2 caricature": linear loop, nonlinear
   output path);
2. one where they **also take part in the oscillation process** (nonlinearity inside the loop).

The paper explains the differing hypotheses and compares numerical results. **I could not read
the comparison itself.** HAL keywords for the record: waves, bell, propagation, physical model,
trombone.

Two secondary statements about what that comparison found:

- **(A, secondary — S4 §4.2)** Cooper & Abel, citing this paper as their ref. [17], state that
  including the backward wave within the oscillatory feedback loop **does affect the fundamental
  frequency and increases the brassiness** of the synthesised output.
- **(A, secondary — S5 §5)** Berjamin et al., citing it as their ref. [45], state that the
  influence of nonlinear propagation on playing frequency had been shown numerically for the
  trombone but only for steady-state regimes, at lower blowing pressures and with a simplified
  nonlinear-propagation model, with reported **deviations of less than 5 cents for weak dynamics**.

### 1.4 The simulation topology actually drawn in ICMC'97 (A — S1 §5, Fig. 7)

Block diagram, read from the figure:

```
         p_i+                      p_o+
[lips model] ──► [delay line] ──────────► [bell]
       ▲                                    │
       │         p_i-                p_o-   │
       └──────── [delay line] ◄─────────────┘
```

i.e. a one-mass lip excitation, **two** delay lines (one per direction, each linear *or*
nonlinear), and a **low-pass filter as the linear bell model**. The paper states that the model
compares two systems, linear vs nonlinear propagation, driven by the same parameters.

---

## 2. THE NONLINEAR ELEMENT (the star) — exhaustive

All of §2 is **(A)** from S1 (Tassart, Msallam, Depalle, Dequidt, ICMC'97), which is the paper
that defines the algorithm. Equation numbers are that paper's own.

### 2.1 Underlying physics

**Eq. (1), §2 "Nonlinear Wave Equation".** First-order nonlinear wave equation for propagation in
the +x direction of a wave φ(x,t) whose propagation speed c(φ) is a function of φ. As printed in
the LaTeX2HTML version:

```
φ_x + c(φ) φ_t = 0                                      (S1, Eq. 1)
```

> **Transcription note (C/A).** I read this image at 10× magnification to be sure of the
> subscripts; it really is printed with `x` on the bare term and `t` on the `c(φ)` term.
> That is **inconsistent with the paper's own Eqs. (2) and (3)**, which treat `c` as a *speed*
> (delay = L/c). The self-consistent form, and the standard simple-wave / inviscid-Burgers form,
> is `φ_t + c(φ) φ_x = 0`. Treat Eq. (1) as printed with a transposed subscript pair.
> Port from Eqs. (2)/(3), which are unambiguous.

The paper attributes the equation to Whitham, *Linear and Nonlinear Waves* (Wiley-Interscience,
1974) — S1 ref. 6.

**Eq. (2).** The equation propagates an initial profile in space with velocity c(φ):

```
φ( x , t + x / c(φ(0,t)) ) = φ(0, t)                     (S1, Eq. 2)
```

Figure 1 of S1 shows successive profiles at x = 0 and x = L — the classic steepening picture.

**Eq. (3) — the key statement that this is a signal-dependent delay.** Propagation time through a
guide of length L from input to output, `d⁺`, depends on the magnitude of the input wave and on L:

```
φ( L , t + d⁺(t) ) = φ(0, t)
with  d⁺(t) = L / c( φ(0,t) )                            (S1, Eq. 3)
```

`d⁺` is called the **input-delay**: it is indexed by the time the sample *entered* the guide.

### 2.2 The speed law and its constant

**Eq. (10), §5 "Acoustics".** The propagation speed as a function of acoustic fluid velocity u:

```
c(u) = c0 + ((γ + 1) / 2) · u                            (S1, Eq. 10)
```

where `c0` is the linear (small-signal) sound speed and `γ` the ratio of specific heats for gases.
S1 attributes this to Whitham (ref. 6, "equation (Eq. 1)").

Notes:

- **This is expressed in particle velocity u, not in pressure.** The nonlinearity coefficient is
  therefore exactly **(γ+1)/2 ≈ 1.2** (for γ = 1.4), multiplying u.
- **(B)** For a simple plane wave, p± = ± ρ0 c0 u±, so the pressure-form equivalent is
  `c = c0 + ((γ+1)/2)·p/(ρ0 c0)`. That matches the Cooper & Abel pressure form
  `v = c0 + β p / P0` with `β = ((γ+1)/2)·(P0/(ρ0 c0)) = ((γ+1)/2)·(c0/γ)`; for γ=1.4 that is
  0.857·c0. (Cooper & Abel instead quote β ≈ 1.2·c0 — see §7 for the discrepancy.)
- **Independent corroboration (A, secondary — S5 §2.1.2, Eq. 4):** the Menguy–Gilbert model as
  written by Berjamin et al. has advection coefficients `a = a0`, **`b = (γ+1)/2`** in
  `∂u±/∂t + ∂/∂x( ±a u± + b (u±)²/2 ) = …`, i.e. exactly the same nonlinear coefficient on the
  velocity. This is the same physics, in flux form.

### 2.3 Input-delay → output-delay conversion — the crux of the algorithm

This is the part that distinguishes the IRCAM formulation from every "just modulate a delay line"
implementation, and it is what must be ported faithfully.

The problem (S1 §3): a delay-line filter is indexed by *output* time — you ask "what delay do I
apply to produce the sample I am emitting now?" But the physics (Eq. 3) gives the delay as a
function of the wave at the *input* time. In the linear case `d⁻ = d⁺` and the distinction
vanishes; when c varies there is no trivial relationship. So a conversion system is required.

**Eq. (4) — two time scales:**

```
∀t ∈ ℝ:   τ⁺(t) ≝ t + d⁺(t)
          τ⁻(t) ≝ t − d⁻(t)                              (S1, Eq. 4)
```

**Eq. (5) — the propagation restated in signal terms** (e = input signal, s = output signal):

```
s ∘ τ⁺ = e
s      = e ∘ τ⁻                                          (S1, Eq. 5)
```

**Eq. (6) — reciprocity of the time scales**, valid under the constraint that τ⁺ or τ⁻ is
reversible:

```
τ⁻ ∘ τ⁺ = Id
τ⁺ ∘ τ⁻ = Id                                             (S1, Eq. 6)
```

> **Port-critical (A/C).** The reversibility constraint is stated by the paper as a *condition*
> (a footnote is attached to it which I did not retrieve). Reversibility fails exactly when
> characteristics cross — i.e. **when a shock forms**. This is the seam where the ISMA'97
> "special treatment based on weak shock theory" (S3) must be spliced in.

**Eq. (7) — delay conversion, functional form:**

```
d⁻ ∘ τ⁺ = d⁺
d⁺ ∘ τ⁻ = d⁻                                             (S1, Eq. 7)
```

**Eq. (8) — the implicit equations in ordinary notation (this is the one to implement):**

```
∀t ∈ ℝ:   d⁻( t + d⁺(t) ) = d⁺(t)
          d⁺( t − d⁻(t) ) = d⁻(t)                        (S1, Eq. 8)
```

**Interpretation given by the paper (S1 §3.1.3, Fig. 3):** the d⁺→d⁻ conversion is itself
**a delay filter whose delay is controlled by its own output**. That feedback loop is the origin
of the nonlinearity of the system. The paper proves causality graphically (Fig. 2): the horizontal
line measuring d⁻ meets the τ⁺ curve to its left, i.e. in the past of the signal — knowing the
past of d⁺ suffices to determine d⁻.

**Eq. (9) — the complete continuous-time model.** Let `g` be the instantaneous one-to-one map from
input wave to input delay, `d⁺ = g(e)` (from Eq. 3, `g(e) = L / c(e)`). Then:

```
s = e ∘ τ⁻ = g⁻¹ ∘ g ∘ e ∘ τ⁻ = g⁻¹ ∘ d⁺ ∘ τ⁻ = g⁻¹ ∘ d⁻  (S1, Eq. 9)
```

So the complete one-way nonlinear propagation model is a **three-stage chain**:

```
wave e ──[ g : wave → delay ]──► d⁺ ──[ delay filter, delay controlled by feedback ]──► d⁻ ──[ g⁻¹ : delay → wave ]──► s
```

> **Port-critical (A).** The time-varying interpolating filter in this formulation **operates on
> the delay signal d, not on the audio wave**. The audio is recovered by inverting g at the
> output. That is materially different from the common "modulate the read pointer of the audio
> delay line" scheme (Cooper & Abel, §7 below). Both appear in the literature; the IRCAM papers
> specify the former.

### 2.4 Discrete-time implementation (S1 §4)

**Fig. 4 — the ideal but NOT realisable discretisation.** `d_n⁺` enters a block `z^(−d_n⁻)` whose
delay is its own output, fed straight back. The paper states this instantaneous (delay-free)
feedback loop is not realisable, citing Karjalainen, Härmä & Laine, "Realizable warped IIR filters
and their properties", ICASSP'97, pp. 2205–2208 (S1 ref. 3).

**Fig. 5 — the realisable discretisation.** The total delay is split around an integer:

```
d_n⁺ ──►[ z^( −(d_n⁻ − p) ) ]──►[ z^(−p) ]──► d_n⁻
                 ▲                              │
                 └────── (d_n⁻) − p ◄───────────┘
```

Read from the figure: an adder combines `p` (with a minus sign) and the fed-back signal (with a
plus sign) to form the control of the first, **fractional and time-varying**, delay block; the
second block is a **fixed integer delay z^(−p)**; the feedback is tapped *after* the fixed delay.
The two delays compose to the required total d_n⁻, and the p samples of pure delay inside the
loop make the loop computable.

**The integer parameter p** (S1 §4, text): can be set freely, but **must be greater than one**.

**Fig. 6 — the complete discrete-time one-way propagation model:**

```
e_n ──►[ g ]──► d_n⁺ ──►[ z^(−(d_n⁻ − p)) ]──►[ z^(−p) ]──► d_n⁻ ──►[ g⁻¹ ]──► s_n
                                ▲                              │
                                └──────────────────────────────┘
```

The paper states explicitly that this whole structure **replaces the classical digital delay line
`z^(−d)`** used for constant propagation speed in a waveguide.

**Fractional-delay filter choice (S1 §4.2 "Filter Approximations"):**

- Because of the feedback loop, a *particularly stable* implementation of the time-varying
  fractional delay is required.
- They therefore focus on **FIR** implementations, and specifically on **Lagrange Interpolator
  Filters (LIFs)**, citing Depalle & Tassart, "Fractional delay lines using Lagrange
  interpolators", ICMC'96 Hong Kong (S1 ref. 1) and Laakso, Välimäki, Karjalainen & Laine,
  "Splitting the unit delay", IEEE SP Magazine 13(1), 1996 (S1 ref. 4).
- LIFs approximate the ideal fractional delay at low frequency and the valid range grows with
  order, **but in this application orders greater than 2 do not significantly improve the
  frequency response.** → order 1 or 2 is the practical choice.

**Aliasing and stability measures (S1 §4.3 "Filter Aliasing"):**

- High-frequency generation is intrinsic to the continuous system, so the paper judges it
  difficult to derive a discrete equivalent guaranteeing a band-limited output for a band-limited
  input; no exact anti-aliasing scheme is offered.
- Pragmatic measures suggested, whenever aliasing occurs:
  1. **oversample the filter**;
  2. **limit the negative slope of the input signal**;
  3. **introduce a low-pass filter in the feedback loop**.

No oversampling factor, slope limit, or low-pass cutoff is given.

### 2.5 What the nonlinear element does, measured (S1 §5.2, Fig. 8)

Simulation comparison, linear vs nonlinear propagation, same parameters. Three spectra are shown
(incoming wave p⁺, outgoing wave p⁻, and slide output pressure), each plotting the first ten
harmonics; axes read from the figure: **frequency 200–2000 Hz, magnitude ~100–170 dB SPL**.

Findings stated by the authors:

- Nonlinear propagation clearly produces high-frequency components, making the sound brassier.
- The nonlinear propagation plays only a subtle role at **low frequencies (below about 800 Hz)**,
  which the paper identifies as the **bandwidth of the bell's reflection function**.
- Even though the motion of the lips is perturbed, **the frequency spectrum of the input incoming
  wave p_i⁺ is not modified much** — which the authors say supports S2's hypothesis that the
  nonlinear propagation has a rather small effect on the self-oscillation process at steady state,
  compared with its effect on the radiated sound.

### 2.6 Shock formation — the criterion (A — S2, Eq. 1)

From the 1996 JASA paper (S2), the critical distance beyond which a shock forms:

```
x_s ≈ 2 γ P_at c / [ (γ + 1) (∂p_m/∂t)_max ]             (S2, Eq. 1)
```

with **γ = 1.4** (named there as the Poisson constant), `P_at` the mean atmospheric pressure, `c`
the speed of sound, and `(∂p_m/∂t)_max` the **maximum pressure rise rate in the mouthpiece**.

Statements attached to it in S2:

- The relevant severity parameter is **not** p_m/P_at but the rise rate — hence the importance of
  the source nonlinearity (lip closure).
- Using their measured data, shocks are expected at fortissimo because **the cylindrical pipe
  segment of the trombone (2 to 3 m) is longer than x_s**.
- Beyond x_s the characteristics-integrated pressure profile becomes multiple-valued; position and
  strength of the shock are estimated from that multivalued solution within frictionless **weak
  shock theory** (S2 cites Pierce, *Acoustics*, 2nd ed., ASA 1989; and Crighton et al., *Modern
  Mathematical Methods in Acoustics*, Springer 1992).
- The shock path in the (x,t) diagram lies along the **bisector of the angle formed by two
  characteristics of the same family** that intersect in the multivalued region; the **weak shock
  speed is the mean of the speeds of the two intersecting characteristics**. Eq. (1) gives the
  first point of the shock path, from which the path is integrated numerically.

---

## 3. Lip valve model

**(A — S3)** The ISMA'97 abstract states only that the trombonist's lips are described by a
**simple one-mass or two-mass model**. No equations, no parameter values.

**(A — S1 §5.2)** The ICMC'97 demonstration model uses **a one-mass lips model as the nonlinear
exciter**. No equations or values given.

**(C)** The actual equations and parameter values used by the Msallam group are **not recoverable
from any document I could read**. They are presumably in the Acta Acustica paper and/or Msallam's
thesis, neither of which I could obtain.

**(B) Standard background I am supplying** — the form such a one-mass ("outward-striking",
Adachi-lineage) lip model normally takes, as written by Berjamin et al. (S5 §3.1, Eqs. 43–47).
**This is NOT Msallam's model; it is a contemporary model of the same class**, included so the
port has a concrete starting point:

- Lip = thin rigid rectangular plate, height h, width l, at angle φ to the axis; projected area
  `A = h · l · sin φ` (S5 Eq. 43). Only the top lip moves; the bottom lip is static.
- Mechanics: `m ÿ + r ẏ + k (y − y_eq) = f(y,t)`, with `y(0)=y0`, `ẏ(0)=y1` (S5 Eq. 45).
- Forcing: `f(y,t) = A ( p_m(t) − p_e(y,t) )` (S5 Eq. 46), where `p_m` is mouth pressure and
  `p_e = p⁺(0,t) + p⁻(0,t)` is the acoustic pressure at the resonator entry (S5 Eq. 44).
- Flow: stationary, incompressible, laminar, inviscid in the mouth and under the lip
  (Bernoulli + mass conservation); the sudden cross-section change behind the lip creates a
  turbulent jet that dissipates all its kinetic energy with no pressure recovery in the
  mouthpiece; this closes the system for `p_e` (S5 Eq. 47).
- Parameter set used by S5 (their Table 2) — **their values, obtained partly from other
  publications and partly by trial and error until self-oscillation occurs**:
  `m = 1.78·10⁻⁴ kg`, `k = 1278.8 N/m`, `r = √(m k)/4`, `l = 10⁻² m`, `A = 10⁻⁴ m²`,
  `p_m = 20·10³ Pa`, and initial/equilibrium values printed as `y0 = 4·10⁻³ m`, `y1 = −4 m/s`,
  `y_eq = 5·10⁻⁴ m`.
  *Caveat: the y-row column alignment is uncertain in my text extraction of that table; treat the
  three y-values as needing re-checking against the PDF before use.* S5 also varies k between
  3000 N/m and 100 N/m to sweep register.

---

## 4. Bell, radiation, losses

**(A — S3, ISMA'97 abstract)** Viscothermal losses, propagation in the bell, and radiation are
**all treated linearly** in the simulation. Nonlinear propagation is confined to the slide.

**(A — S1 §5.2 and Fig. 7)** The ICMC'97 demonstration model represents the bell by **a low-pass
filter for the linear modelling of the bell**, and cites the ISMA'97 paper (S1 ref. 5) for it.
No order, no coefficients, no cutoff.

**(A — S1 §5.2)** Related number: the nonlinear propagation matters little below ~**800 Hz**,
which the paper identifies with the **bell reflection-function bandwidth** — i.e. their bell
reflection function is effectively a low-pass with a corner in that region.

**(A — S2)** Physical justification for treating the horn linearly and for the whole
decomposition: the most relevant high frequencies are radiated very efficiently at the horn, so
reflection at the pipe termination is neglected for them and the propagation can be taken as a
simple wave into a uniform region.

**(A — S1 §5.1)** Losses: both measurements (citing S2) and dimensionless analysis (citing the
ISMA'97 paper) show that **nonlinear propagation AND viscothermal boundary-layer effects must both
be taken into account** to describe propagation in the trombone slide. Higher-order contributions
(nonlinear interactions in the main flow, nonlinearities in the boundary layers) are neglected.
The pipe solution is a linear combination of an incoming and an outgoing plane wave, **both**
subject to viscothermal losses and nonlinear distortion. Eq. (10) itself is stated for the
frictionless case.

**(C)** The *form* of the viscothermal loss filter Msallam et al. used (order, design method,
whether per-sample or lumped per traversal) is not stated in any source I could read.

**(B) Background on what that loss law is.** Wall (boundary-layer) losses in a cylindrical bore go
as √ω and as 1/R. In the Menguy–Gilbert formulation (S5 §2.1.2, Eqs. 3a–4) the loss term is a
half-order time derivative with coefficient

```
c(x) = C · a0 · √ν / R(x),      C = 1 + (γ − 1)/√Pr
```

and the volumic (bulk) dissipation coefficient is `d = ν_d/2` with
`ν_d = ν ( 4/3 + µ_v/µ + (γ−1)/Pr )`, `a0 = √(γ p0 / ρ0)`. Air at 15 °C (S5 Table 1):
`γ = 1.403`, `p0 = 10⁵ Pa`, `ρ0 = 1.177 kg/m³`, `Pr = 0.708`, `ν = 1.57·10⁻⁵ m²/s`,
`µ_v/µ = 0.60`. **Labelled B: this is a 2016 paper's formulation, not Msallam's.**

---

## 5. Parameter table — every number I could actually source

| Quantity | Value | Source (tier) |
|---|---|---|
| Nonlinearity coefficient (velocity form) | `(γ+1)/2` — the coefficient multiplying u in `c(u) = c0 + ((γ+1)/2)u` | S1 Eq. 10 (A, primary) |
| γ (ratio of specific heats / Poisson constant) | 1.4 | S2, text under Eq. 1 (A, primary) |
| Trombone cylindrical (slide) pipe segment length | **2 to 3 m** | S2, shock-distance discussion (A, primary) |
| Instrument measured | **A. Courtois trombone type 155**, A. Courtois mouthpiece **type 10 PM** | S2, Fig. 1 caption (A, primary) |
| Microphone positions | mouthpiece (p_m); end of cylindrical pipe section (p_p); **4.5 cm from horn exit on axis** (p_h) | S2, Fig. 1 caption (A, primary) |
| Frequency range of the ICMC'97 spectral comparison | ~200–2000 Hz, first ten harmonics | S1 Fig. 8 (A, primary) |
| Magnitude range in those plots | ~100–170 dB SPL | S1 Fig. 8 (A, primary) |
| Bell reflection-function bandwidth | ~**800 Hz** (below this, nonlinearity has only a subtle role) | S1 §5.2 (A, primary) |
| Loop integer delay p (Fig. 5/6) | integer, free, **must be > 1** | S1 §4.1 (A, primary) |
| Lagrange interpolator order | **≤ 2** (higher orders give no significant improvement here) | S1 §4.2 (A, primary) |
| Sample rate | **NOT GIVEN in any source I read** | — (C) |
| c0 / temperature | **NOT GIVEN by the Msallam group sources** | — (C) |
| Bore radius | **NOT GIVEN by the Msallam group sources** | — (C) |
| Lip mass / stiffness / damping | **NOT GIVEN by the Msallam group sources** | — (C) |
| Blowing pressure | **NOT GIVEN by the Msallam group sources** | — (C) |

Supplementary numbers, **all tier-B/secondary**, usable as sanity anchors only:

| Quantity | Value | Source (tier) |
|---|---|---|
| β for air, pressure form | "approximately 1.2 times the nominal speed of sound" | S4 §2.1 (B/secondary — see §7 caveat) |
| Static atmospheric pressure used | 10⁵ Pa | S4 §2.1, S5 Table 1 (B) |
| 140 dB SPL ⇒ peak-to-peak pressure | 679 Pa ≈ 0.7 % of P0 | S4 §2.1 (B) |
| ⇒ peak-vs-trough speed difference | 0.814 % of c0 | S4 §2.1 (B) |
| ⇒ over 2 m propagation, arrival-time difference | **48.7 µs**, ≈ ¼ period of 5 kHz | S4 §2.1 (B) |
| Measured brass mouthpiece levels | can exceed 160 dB | S4 §1, citing S2 (B) |
| Backus & Hundley 160 dB mouth pressure | ≈ 3 kPa oscillation amplitude | S2 (A, but about *other* authors' data) |
| Air at 15 °C | γ=1.403, p0=10⁵ Pa, ρ0=1.177, Pr=0.708, ν=1.57·10⁻⁵, µ_v/µ=0.60 | S5 Table 1 (B) |
| Example resonator in S5 | D = 1.4 m, R = 7 mm, N_x = 100, CFL ε = 0.95 | S5 §2.4.1, §4.2 (B) |

---

## 6. Validation — what they compared against

**(A — S2, the measurement paper.)** This is the empirical basis the synthesis model was built to
reproduce:

- Trombone measured with three sensors: **Kistler 603-A** acceleration-compensated piezoelectric
  gauges (coated with a 0.1 mm silicone layer to avoid thermal effects) with **Kistler 5007**
  charge amplifiers, bandwidth **1 Hz – 180 kHz**, plus a **B&K 1/8-inch condenser** microphone
  outside the pipe.
- **Dynamic levels compared: piano (p), mezzo-forte (mf), fortissimo (ff)** — Figs. 2 and 3, at a
  low and a higher pitch respectively.
- Observed: increasing nonharmonicity of the mouthpiece pressure and nonlinear wave steepening in
  the pipe pressure with increasing playing level; **stepwise pressure jumps**; at ff on the higher
  note the pressure-rise time is down to the wave transit time across the microphone face.
- **Schlieren flow visualisation** with a **Nanolite spark discharge (80 ns)** shows a sharp wave
  front at the horn exit, at most **a millimetre thick**.
- Comparison of p_m, p_p and p_h shows the radiated sound is dominated by high frequencies.
- Conclusion: shocks are formed in a trombone under typical playing conditions; nonlinear
  propagation is expected to be musically relevant for the trumpet too.
- **So: the nonlinearity matters at forte/fortissimo, and is essentially absent at piano.**

**(A — S1 §5.2)** The ICMC'97 validation is purely a **simulation A/B**: same model and
parameters, linear vs nonlinear delay lines, compared on the magnitudes of the first ten harmonics
of p⁺, p⁻ and the slide output pressure. No comparison against measured instrument data in that
paper.

**(C) The Acta Acustica paper's validation is unknown to me** — I could not read it. Its abstract
says only that it compares the numerical results of the two implementations.

**(C) Brassiness metric: NOT COVERED.** The modern brassiness parameter B comes from the
Gilbert / Menguy / Campbell lineage (JASA 123(4), 2008) which I could not fetch, and postdates
the Msallam papers by eight years. Nothing in the sources I read defines a brassiness metric;
S1 uses harmonic magnitudes and S5 uses spectral centroid.

---

## 7. Secondary source: the Cooper & Abel formulation (clearly labelled SECONDARY)

S4 is not by the Msallam group but is the most explicit published derivation of the
amplitude-dependent delay, and is useful as a cross-check. **(A, secondary)** throughout:

- Wave equation `∂²p/∂t² = v(x,t)² ∂²p/∂x²` (S4 Eq. 1) with
  **`v(x,t) = c0 + β p(x,t)/P0`** (S4 Eq. 2); β/P0 > 0 so peaks outrun troughs.
- Directional first-order pair: `∂p/∂t ± (c0 + β p/P0) ∂p/∂x = 0` (S4 Eq. 3).
- Implicit solutions `p(x,t) = g( x − (c0 + βp/P0) t )` (Eq. 4) and
  `p(x,t) = h( t − x/(c0 + βp/P0) )` (Eq. 5).
- Discretisation with c0 = 1 sample position per sample interval, first-order differences
  (Eqs. 6–7), giving
  **`p(n,t) = α p(n,t−1) + (1−α) p(n−1,t−1)` with `α = −β p / P0`** (Eqs. 8–9);
  and the alternative **`p(n,t) = η p(n−1,t) + (1−η) p(n−1,t−1)` with
  `η = (−β p/P0)/(1 + β p/P0)`** (Eqs. 10–11).
- **Equivalent statement (S4 Eq. 12): at each time step the waveform at position n is replaced by
  its value at position `n − 1 − β p/P0`.** I.e. a cascade of per-element amplitude-dependent
  fractional delays (their Fig. 4).
- Implementation advice: **upsample then apply a low-order FIR interpolation** — linear per
  Eqs. (8)/(10), or **4th-order Lagrange**, with modest upsampling (×2 or ×4). The HF droop of FIR
  interpolation is described as not unwelcome, being loosely similar to damping in air. First-order
  allpass is possible but has two drawbacks: it is dispersive, and audio-rate coefficient
  modulation introduces distortion artifacts.
- **Bidirectional waveguide (S4 §4.1, Fig. 5): in a bore the sound speed depends on the TOTAL
  pressure, not on the individual travelling waves — so at every position the SUM of left- and
  right-going pressures modulates both variable delays.**
- **Lumped simplification (S4 §4.2):** approximate n cascaded variable delays by a single combined
  delay of value `n × (1 + β p/P0)`; sum the two directions at a sparse set of positions, because
  the in-bore pressure is dominated by low (hence low-spatial-frequency) components.
- Shock handling (S4 §5.1): physically, samples computed to be overtaken by earlier
  higher-amplitude samples should be discarded; the authors report that **omitting that discard
  sounds brighter and, in their opinion, more musically appealing**, and that with shocks omitted
  the effect becomes equivalent to phase/frequency modulation.

> **Caveat / discrepancy (C).** S4 says β for air is approximately 1.2 c0 and uses `β p / P0`.
> With `β = 1.2 c0` and `P0 = 10⁵ Pa`, `Δc/c0 = 1.2 p/P0`; but the physically standard result
> (and S1 Eq. 10 recast, §2.2 above) is `Δc/c0 = ((γ+1)/2)·u/c0 = ((γ+1)/(2γ))·p/P0 ≈ 0.857 p/P0`.
> S4's own worked example (0.7 % pressure ratio → 0.814 % speed difference) is consistent with a
> factor of ~1.2 applied to a *peak-to-peak* ratio, so the two may reconcile as a peak vs
> peak-to-peak bookkeeping difference. **Do not carry S4's 1.2 into a port without re-deriving.**
> S1 Eq. (10) is the authoritative form: coefficient `(γ+1)/2` on *particle velocity*.

---

## 8. Real-time / simplification differences: ICMC'97 vs the full model

**(A)** Comparing S1 (the DSP paper, with its own demo model) against S3 (the full trombone model):

| Aspect | ICMC'97 demo (S1 §5.2) | Full trombone model (S3 abstract) |
|---|---|---|
| Lips | one-mass | one-mass **or two-mass** |
| Slide | two delay lines, linear *or* nonlinear | nonlinear propagation inside the slide, method of characteristics; **slide length variation supported** |
| Viscothermal losses | not mentioned in the demo topology | **included**, linearly |
| Shocks | not mentioned; the whole Eq. (6) derivation assumes reversible time scales | **special weak-shock-theory treatment when shocks occur** |
| Bell | a single low-pass filter | **linear propagation in the bell + radiation** |
| Forward/backward coupling | two independent delay lines | nonlinear interaction between forward and backward waves **explicitly neglected** |
| Purpose | demonstrate the algorithm | reproduce trombone brightness at high playing levels |

And **(A, abstract-level only)** the Acta Acustica paper adds the axis the earlier papers do not
have: nonlinearity **outside** the oscillation loop (radiation only) vs **inside** it.

---

## 9. Open questions for the port (tier C)

1. **The map g.** Is `d⁺ = g(e) = L/c(e)` used exactly (a division per sample), or linearised to
   `d⁺ ≈ (L/c0)(1 − ((γ+1)/2)·u/c0)`? S1 states only that g is an instantaneous one-to-one
   relation. Also, `g⁻¹` must be applied to the *delay* signal to recover the wave — the exact
   inverse used is not written down.
2. **Which quantity is interpolated.** S1's structure filters the **delay signal** d⁺→d⁻ and maps
   back through g⁻¹; the widely-copied Cooper & Abel scheme interpolates the **audio** directly.
   These are not the same algorithm. Which behaviour Msallam's Acta Acustica implementation uses,
   and how they differ perceptually, is unresolved here.
3. **p, the loop integer delay.** Only constrained to be an integer > 1. Its practical value
   (and its effect — it delays the feedback of the delay estimate by p samples) is unstated.
4. **Oversampling factor and the feedback low-pass.** S1 recommends oversampling, input negative
   slope limiting, or a low-pass in the feedback loop, with **no values for any of the three**.
5. **Shock algorithm.** ISMA'97 says weak shock theory is applied; the discrete algorithm is
   described nowhere I could read. Note that the ICMC'97 derivation's validity condition (Eq. 6
   reversibility) is precisely what a shock violates — so the shock handler is not an optional
   embellishment, it is the repair of a broken assumption.
6. **Viscothermal loss filter form** actually used in the slide (order, design, placement).
7. **Bell reflection and transmission filters**: form, order, coefficients, and how the ~800 Hz
   reflection bandwidth is realised. Nothing beyond "low-pass" is recoverable.
8. **Lip model equations and all lip parameter values** — none available from the Msallam group.
9. **Slide-length variation.** S3 says the characteristics method can include it and that this is
   what makes the formulation a time-varying fractional delay. The formulation is not available.
10. **All the base numbers**: sample rate, c0/temperature, bore length and radius as simulated,
    blowing pressure, dynamic-level scaling. Only the physical trombone's 2–3 m cylindrical
    segment (S2) is sourced.
11. **How the two Acta Acustica variants differ numerically** — the one thing that paper is
    specifically about, and the one thing I could not read. Two secondary claims exist about it
    (§1.3): a fundamental-frequency shift plus increased brassiness when the backward wave is in
    the loop (S4), and <5 cents deviation at weak dynamics in steady state (S5).
12. **Brassiness metric** — not defined in anything I read; would need Gilbert/Menguy/Campbell 2008.

---

## 10. Source URLs, for re-fetching

- ICMC'97 paper (full text, http only): `http://recherche.ircam.fr/anasyn/tassart/these/icmc97/article.html`
  → sections `node1.html` … `node7.html`; equations are `img1.gif` … `img43.gif` in the same directory.
- Author index page listing all of Tassart's papers: `http://recherche.ircam.fr/anasyn/tassart/these/these.en.html`
- ISMA'97 abstract (authors' page): `http://recherche.ircam.fr/anasyn/tassart/these/isma97/abstract.html`
- ISMA'97 slides (PostScript, bitmap math): `http://recherche.ircam.fr/anasyn/tassart/these/isma97/presentation_slide.ps.gz`
- Shock waves in trombones (JASA 1996, full PDF): `https://pure.tue.nl/ws/files/1503964/617406.pdf`
  (also mirrored at `https://theorie.ikp.physik.tu-darmstadt.de/qcd/moore/ph225/shock.pdf`)
- IRCAM BASIRcam, Msallam records: `http://articles.ircam.fr/index.php?Action=Results&Auteur=Msallam`
- HAL record for the Acta Acustica paper (no file): `https://hal.science/hal-01105577`
- Cooper & Abel DAFx-10: `https://www.dafx.de/paper-archive/2010/DAFx10/CooperAbel_DAFx10_P77.pdf`
  (their sound examples: `http://ccrma.stanford.edu/~ccooper/DAFx2010/examples`)
- Berjamin et al.: `https://arxiv.org/pdf/1511.04247`

### Not yet tried, if someone wants the Acta Acustica text

- Inter-library loan / institutional access to Acta Acustica united with Acustica 86(4), 2000.
- Msallam's 1998 thesis via Sorbonne / Paris 6 library (theses.fr, SUDOC) — catalogued at IRCAM
  as BASIRcam record 341 but with no online copy.
- Rodet & Vergez, "New algorithm for nonlinear propagation of a sound wave, application to a
  physical model of a trumpet", J. Signal Processing 4(1):79–87/88, 2000 — the contemporaneous
  IRCAM alternative algorithm; not fetched in this session.
- Smyth & Scott, "Trombone Synthesis by Model and Measurement", EURASIP J. Adv. Signal Process.
  2011:151436 — open access, would supply measured trombone bell reflection/transmission filters;
  Springer redirected to an auth endpoint in this session, but `https://summit.sfu.ca/item/11130`
  looks like an open mirror.
