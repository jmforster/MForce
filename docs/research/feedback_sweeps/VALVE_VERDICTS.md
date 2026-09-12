# Valve rounds 1-4 — verdicts and what they bought (2026-09-08)

One day, four generations, two engine bugs, one ceiling named. Tools:
tools/gen_feedback_valve1.py (series), tools/gen_feedback_valve2.py
(drive-coupled). Base: oboe1.

| Round | Coupling | Matt's verdict |
|---|---|---|
| 1 series | bandpass IN the signal loop | "generic reed"; slow attack, not breathy, not brassy; off-center ratios physics-dead |
| 2-3 drive | valve gates Junction.drive (LipOpen closure/cap) | unauditionable — tuning wrecked by the compensation-walk bugs (backlog 66) |
| 4 final | same, on the fixed engine, full grid in tune | "fine... honestly I can't hear any difference among them. Vibrato's nice. Basically oboe_default with less honk." |

## Standing conclusions

- **Operating-point coupling is perceptually null** even at k=1.0 (lip
  swinging drive over the full closed-to-double range): criticals
  identical to stock, harmonic-profile shifts 2-5 dB, ears say "same."
  Together with round 1: neither filtering the junction's input nor
  scaling its window escapes the reed basin. The memoryless
  single-input curve is the ceiling (see the two junction specs,
  2026-09-08).
- Incidental trait, Matt's words: drive-coupled valve ≈ "oboe_default
  with less honk" — worth remembering as a honk-tamer knob independent
  of the brass hunt.
- The 2D junction spec's §2 gate (curve-SHAPE coupling via the
  audio-rate morph pin) is the one existing-primitive axis NOT yet
  tried — run it before any 2D v1 code. Its wiring adds a consumer to
  a loop member, so expect the uniform 1-sample tuning residual
  (backlog 65/66 remainder) — uniform, so in-round A/B stays valid.
- Vibrato: Pitch_vibrato 0.015 reads as a huge pitch swing on this
  family (loop swept across a high-Q in-loop resonance); 0.003 is the
  family's working depth ("vibrato's nice").
