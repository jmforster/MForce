# Fable1 results — partial motion exploration

Task: docs/Fable1.md. Design: docs/superpowers/specs/2026-07-03-partial-motion-design.md.
Renders: `renders/fable1/*.wav` (C major scale C4→C5, 8 × 1.6 s notes).
Patches: `patches/fable1/*.json`. Generator: `tools/gen_fable1_patches.py`.
Sanity metrics: `tools/check_fable1_renders.py`.

## Code changes (all in `engine/include/mforce/source/additive/partials.h`)

One new "motion layer" in the `Partials` base class — four independent,
default-inert features, available to Full/Sequence/Explicit/Composite
partials. All built on a shared primitive: a smoothed random walk whose
segments can re-roll their own length and excursion (`*Evolve` configs), so
the movement is non-stationary — the character of the motion itself changes
over time. State allocates in `partials_prepare` (per note), never in the
sample loop.

1. **Frequency motion** — per-partial pitch offset in cents.
   `motionDepth1/2` (blended by new `motionEnv` param, same idiom as `bwEnv`),
   `motionHz`, `motionCoherence`, `motionEvolve`, `motionScale`.
   The key knob is **coherence**: each partial's offset mixes one *shared*
   walk (all partials move by the same cents — proportional Hz, fuses via
   common fate, like vibrato) with its own independent walk (broadens lines,
   and at 0 reproduces the known "bunch of separate notes" failure).
2. **Amplitude shimmer** — same walk machinery on per-partial gain.
   `shimmerDepth1/2` (+ `shimmerEnv`), `shimmerHz`, `shimmerCoherence`,
   `shimmerEvolve`.
3. **Onset dispersion** — per-note random onset delay + fade per partial;
   phase keeps running while gated so partials *enter* as already-running
   oscillators. `onsetSpread`, `onsetTilt` (+1 = low partials first, high
   harmonics bloom later), `onsetFade`.
4. **Energy trading** — adjacent partial pairs share one slow walk with
   opposite signs: energy sloshes between neighbors, pair sum ~constant.
   `tradeDepth`, `tradeHz`.

## Render × technique table

| Render | Techniques | Sanity notes |
|---|---|---|
| 00_control | none (base viola + vibrato) — baseline for comparison | attack/sustain flux 0.026/0.029 |
| 01_mot_settle | freq motion attack: 90→4 cents over 0.35 s, coherence 0.6, evolve 0.3 | attack flux 1.36× control |
| 02_mot_coherent | sustained 6-cent motion, coherence 0.9, evolve 0.6 | decorrelates fully from control; flux ≈ control (movement is pitch-domain, subtle by design) |
| 03_mot_incoherent | identical to 02 but coherence 0 — A/B test of the segregation theory | listen against 02: does 0.9 fuse where 0 splits? |
| 04_onset_bloom | onset dispersion only: 150 ms, low partials first | hi-band arrives +11 ms after lo-band |
| 05_onset_scatter | random-order onsets over 250 ms + motion settle 60→5 cents | highest attack flux (2.7× sustain) |
| 06_trade_slosh | energy trade 0.5 @ 1.2 Hz + mild coherent motion | sustain flux ~6% above control |
| 07_shimmer_amp | amplitude shimmer 0.45, coherence 0.3, evolve 0.5 | stays phase-correlated with control (amp-only), gain wander confirmed |
| 08_expand_converge | existing ExpandRule: ±2 sub-partials/harmonic, cluster 1.2-semi wide + detuned → collapses to clean harmonics via multEnv/amplEnv | attack flux 2.4× sustain; the "order out of chaos" idea from the checkpoint |
| 09_bw_coupled_attack | bandwidth env 0.85→0 in 120 ms + onset bloom + motion settle 45→4 cents — noise burst carried *by the partials*, no parallel noise source | attack flux 2.0× sustain, +11 ms hi lag |
| 10_kitchen_sink | onset bloom + motion settle 70→5 + evolving sustain motion + trade 0.35 + shimmer 0.25 | attack 1.8× sustain; the best-guess combination |

## What the metrics do and don't say

Confirmed mechanically: all renders non-silent, correct length; chaos-settle
patches show elevated attack-phase spectral flux that settles; onset-tilt
patches show delayed high-band arrival; every feature demonstrably alters the
waveform vs control. **Not evaluated:** whether any of it sounds credible,
shimmering, or musical — that's the audition.

## Suggested listening order

02 vs 03 first (the coherence A/B — tests the theory behind the whole layer),
then 00 vs 02 (does coherent motion add life?), then 01 / 08 / 09 for the
three different attack strategies, then 10.

---

# v2 batch (post-audition dial-ups) — renders/fable1_v2/

Audition findings applied (Matt, 2026-07-03): independent movement segregates
only at large depth (v1 3 > 2); trade/shimmer too subtle; noise via partials
must be **addition/subtraction** of partials, never movement of resident
partials; onset bloom possibly inaudible at 150 ms.
Generator: `tools/gen_fable1_v2_patches.py`. No engine changes.

| Render | Change vs v1 | Sanity notes |
|---|---|---|
| v2_01_mot10 | incoherent motion depth ladder: 10 cents | sustain flux ≈ control (pitch-domain movement, flux-blind) |
| v2_02_mot18 | 18 cents | " |
| v2_03_mot30 | 30 cents | " — the ladder maps where texture → segregation |
| v2_04_trade80 | trade 0.8 @ 1.2 Hz | strongest sustain-evolution signal (flux +10% vs control) |
| v2_05_trade50_3hz | trade 0.5 @ 3 Hz | faster slosh |
| v2_06_shimmer70 | shimmer 0.7 | sustain flux +8% |
| v2_07_shimmer_mot | shimmer 0.55 + incoherent motion 10 cents | combined evolution |
| v2_08_cluster_decay | stationary inharmonic cluster, amplitude-only decay (no position movement at all) | attack flux 2.6× sustain |
| v2_09_burst_short | v1-09 minus motion settle; burst 120→50 ms, onsets 60 ms | attack ratio 1.08 (short burst integrates small) |
| v2_10_bloom400 | onset bloom 150→400 ms audibility probe | hi-band lag now 75 ms (v1: 11 ms) |

---

# v3 batch — renders/fable1_v3/

Changes: (1) cutoff fix in full_additive_source.cpp — past-16 kHz partials now
gate individually (`continue`) instead of killing everything above them
(`break`), which was the "aliasing" Matt heard on v2_03's high notes at
30-cent depth; (2) viola partials extrapolated 48 → 96 via power-law tail fit
(log-log fit over harmonics 25–48, anchored at 48 — smooth seam, decays to
~3e-5 by 96); (3) new note ladder C2 G2 C3 G3 C4 G4 C5 G5 C6 D6 E6 exercising
the bottom (all 96 partials sound at C2) and the cutoff at the top.
Generator: `tools/gen_fable1_v3_patches.py`.

v2 audition drove the design: trade/shimmer confirmed as *sustain-evolution*
(spectral-envelope / "vowel" drift — that is the mechanism, not an artifact);
18-cent incoherent motion validated; cluster attack still segregating →
2×2 isolation of level/decay vs spacing (harmonicity-theory test).

| Render | Recipe | Sanity notes |
|---|---|---|
| v3_00_control96 | 96-partial base, no features — new baseline | — |
| v3_01_mot30_fix | 30-cent incoherent motion, post-cutoff-fix | E6 max sample-step now ≤ control (was the chatter source) |
| v3_02_cluster_tight | cluster: original level/decay, spacing 1.2→0.3 semis | attack 2.5× sustain |
| v3_03_cluster_low | cluster: original spacing, level 0.5→0.25, decay 0.4→0.15 s | attack 2.2× |
| v3_04_cluster_tight_low | both axes | attack 1.8× |
| v3_05_sustain_stack | mot18 incoherent + trade 0.5 @ 1.2 Hz + shimmer 0.4 — candidate viola sustain | — |
| v3_06_attack_stack | sustain stack + 50 ms bw burst + 150 ms low-first bloom + tight/low cluster — first full recipe | attack 3.2× sustain, hi-lag 43 ms |

Cluster listening note: if v3_02 (tight, original level) already stops
sounding like mashed piano keys, the segregation was spacing (distance from
the parent harmonic); if only v3_03 (wide but quiet/fast) improves it, it was
level/duration; if neither, the harmonicity-grouping theory stands and
inharmonic clusters are the wrong tool for attack noise.

v3 audition: **v3_04 cluster_tight_low is the winner** (both axes needed);
sustain_stack "rich, stringy"; attack_stack "very nice" but attack phase too
distinct from sustain — attack effects decayed to exactly zero.

---

# v4 batch — renders/fable1_v4/ (attack residue)

Attack content no longer decays to zero: cluster fade env gets a
`sustainLevel` floor; bandwidth burst decays to `bandwidth1 > 0` (permanent
slight noisiness — which is also what the ml-ears measurement says real viola
sustain has and ours lacked). Generator: `tools/gen_fable1_v4_patches.py`.

| Render | Recipe | Sustain flux (control 0.026) |
|---|---|---|
| v4_01_cluster_res10 | cluster_tight_low, 10% residual cluster | 0.027 |
| v4_02_cluster_res25 | cluster_tight_low, 25% residual | 0.028 |
| v4_03_bw_floor | burst → permanent bandwidth floor 0.06, isolated | 0.038 |
| v4_04_full_res | sustain stack + cluster res 15% + bw floor 0.05 + bloom | 0.041 |
| v4_05_full_res_hi | same, residue up: cluster 30%, floor 0.10 | 0.050 |
