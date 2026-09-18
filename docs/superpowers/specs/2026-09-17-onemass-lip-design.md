# One-mass lip → full Msallam chain ("the actual brass attempt")

2026-09-17. Companion to 2026-09-17-nonlinear-bore-design.md (steepener =
validated, REVIEW 66, Matt: "brightens in a brassy way"). This spec puts the
STATE-OF-THE-ART lip on the front: the published one-mass outward-striking
model, replacing our memoryless valve curves (the named 09-08 ceiling:
memoryless single-input junction = reed family = "saxy not brassy").
Full chain = the published trombone decomposition:

    one-mass lip (nonlinear, DYNAMIC) → bore loop → steepener (out-of-loop)
    → bell low-pass → out

## The model (sourced)

Digest §3 (docs/research/nonlinear_bore/MSALLAM_DIGEST.md): Msallam's own
lip equations are unrecoverable (tier C), but the same-class contemporary
one-mass model is fully written out in Berjamin et al. arXiv:1511.04247
§3.1 Eqs. 43–47 with a complete parameter set (their Table 2). FETCH THE
ARXIV PDF and transcribe Eqs. 43–47 exactly before implementing — the
digest's summary is not the implementation source; the y-row values in its
Table-2 transcription are explicitly flagged uncertain.

Shape of the model (digest summary, to be checked against the PDF):
- Lip = mass-spring-damper: m·ÿ + r·ẏ + k·(y − y_eq) = A·(p_m − p_e),
  p_m = blowing (mouth) pressure, p_e = pressure at the bore entry
  (p⁺ + p⁻ — i.e. the LOOP feeds back into the lip force).
- Flow through the lip opening: Bernoulli + mass conservation, jet
  dissipates in the mouthpiece (Eq. 47 closes p_e); flow only when open
  (y > 0 — clamp).
- Table 2 starting values: m = 1.78e-4 kg, k = 1278.8 N/m, r = √(mk)/4,
  l = 1e-2 m, A = 1e-4 m², p_m = 20e3 Pa; k swept 100–3000 N/m for
  register.

## Mapping onto MForce primitives (zero or near-zero engine code)

- **Mass-spring-damper = Biquad in resonance mode** (frequency/radius pins).
  This is exactly STK's own lip realization, so the class is proven on this
  node. f_lip = (1/2π)·√(k/m); radius ↔ damping r. Input = force signal
  (scaled pressure difference), output ∝ displacement y.
- **KEYTRACK f_lip**: brass players retune the lip near the note; make
  f_lip = ratio · f0 with ratio an axis (e.g. 0.8 / 1.0 / 1.2). This is
  the register-selection mechanism the memoryless lip cannot have.
- **y > 0 clamp / opening curve**: Shaper (half-wave curve).
- **Multiplications** (y⁺ × √Δp etc.): via amplitude pins (any node's
  amplitude pin is a signal×signal multiply) or Shaper composition. If the
  Bernoulli √ proves unwireable, a linearized flow (u ∝ y⁺·Δp) is an
  acceptable degraded cell — LABEL it as such, don't silently substitute.
- **Bore** = the loop-family skeleton (DelayLine + losses + DC block), the
  proven chassis from the feedback-loops campaign; the lip subgraph replaces
  the memoryless junction. p_e feedback = tap of the bore entry back into
  the lip force sum.
- **Steepener + bell** on the tapped output exactly as gen_nlbore_probe1.py
  wired it (tap-not-ref).

Physical-units discipline: pick an explicit pressure↔signal scaling and
document it in the gen script; every fixed constant sorted physical-Hz vs
normalized (the STK-port lesson).

## Round design

- Cells: f_lip/f0 ratio axis × blowing-pressure axis (soft/loud — ignition
  and brightening must both ride on p_m, that's the brass behavior) ×
  steepener on/off; one memoryless-lip control cell (harness lip, same
  bore) for the A/B that answers "did DYNAMICS matter?"
- Gates: ignition across C3–C5 (pitch-gated lock probe, the harness
  pattern); lock within ±40c (intonation trim can come later — timbre round
  first); dose-response of brightness on p_m; feature audibility; level
  ceiling; ears ≤ 8.
- Success = Matt hears brass-ness the memoryless control lacks. Failure
  after 2 honest wiring attempts = report the specific wall (which term
  wouldn't wire); that wall is the design input for a LipSource node — do
  NOT build the node this round.
