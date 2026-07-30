# DSP lane — review queue

## Awaiting Matt

### 1. Expand round 3 [listen] (run 9)
renders/expand_sweep3/ (9). Your requested variants: fifth_r2_p10 (10
base partials), swirl_r1/swirl_r2, wide2narrow_002/005/008 (20/50/80 ms
attacks), morph_quick (chord collapses to Leslie in ~0.2 s, then holds),
morph_audio_30 + morph_audio_110 (audio-rate morphing — 30 Hz is the
batch novelty leader at 83.6). Verdict decides: keepers -> named patches.

### 2. FormantSequence round 2 [listen] (run 9)
renders/formantseq_sweep2/ (12). Your weight diagnosis was right: w5/w9
now put the active formant 16-20 dB above the valley (round 1's fmtWt 2
was <=3x on partials the rest of the spectrum buried). Same 5 regimes at
_w5 and _w9 + vowel_glide_flat (flatter source) + control_static_w5.
Verdict decides: usable weight range; which morphs graduate.

### 3. FM alias A/B v2 [listen] (run 9)
renders/fm_oversample2/ — your spec: 5 Hz freq LFO, notes C4-C8, os1 vs
os8 pairs. Measured audibility gradient: stripped-alias energy is -16.8
dB of signal at C4 rising to 0 dB at C8 (at the top the aliasing is as
loud as the tone). Verdict decides: oversample default policy for
bright/high FM.

### 4. Clarinet attack report — and a proposed new engine feature [read]
research/ml_ears/out/clarinet_attack_report.md (+4 plots). Your
inverse-frequency law measured exact: rise t50 = 29.3*f0^-0.84 (350-480ms
at E3/G3 -> 60-110ms above 1kHz). The surprise: breath LEVEL is
register-flat and equals the sustain noise floor — breathiness is
EXPOSURE TIME (noise precedes tone by 30-290ms, longer at low f0).
Noise = band-passed hiss, centroid ~2.9kHz, genuinely between-line.
Per-partial AM bandwidth is RULED OUT as the breath (multiplicative with
partial amplitude -> cannot precede the tone).
DECISION NEEDED: the report proposes a new feature — a formant-shaped,
constant-level noise bed gated at note-on with the TONE delayed behind it
(0.15/0.10/0.04s by register), coupled via shared formant + shared gate +
tone-referenced level. This is deliberately NOT the rejected independent
parallel noise layer — the coupling is structural — but it's close enough
to that dead end that I want your read before building it. Verdict
decides: build the noise-bed feature (then clarinet CMA-ES with it), or
attempt CMA-ES first with existing features to see how far it gets.

### 5. Curve-dropdown: stale binary, not a bug [look, 1 min]
Your exe predated the curve-editor commit. Restart mforce_ui, load
v6_04_bwfloor_curve.json, Add curve -> vla_partials: bandwidth1 is
mid-list (scroll; "(has curve)" annotation added). Delete
mforce_ui_running_backup2.exe from build/tools/mforce_ui/Release/ after
closing the old instance.

## Resolved

2026-07-30 (Matt, folded in run 9):
- v6 curves: all fine, taste-level — **lo adopted as default viola
  recipe** (patches/viola_default.json); tunable in UI Curves tab.
- FormantSequence round 1: "all nearly identical, subtle — FormantWeight
  too low?" — mechanically right: fmtWt 2 boosts in-band <=3x while the
  out-of-band partials dominate. Round 2 regenerating at w5/w9 + a
  flat-rolloff variant with formant-dominance verified in dB.
- Expand round 2 ranking (verbatim, reference):
  > fifth_r2 best — recursion adds pleasing bass "whoosh", even lusher
  > than r1, bump base partials to 10 next. leslie_swirl_combo best/most
  > subtle of the leslies (po softens) — try recurse 1 and 2. micro_r2 ==
  > leslie_micro_r2 (slot wasted, reuse). leslie_wide8 least subtle but
  > usable. wide2narrow: useful musical attack IF much faster — 0.02/
  > 0.05/0.08 s versions. fifth_leslie_morph: super cool sound effect —
  > quick-attack version + audio-rate morphing. pi_swept: even MORE
  > villain vibe. narrow2wide: very cool, special effect. semitone_r2 ~
  > r1, sound effect. breathe_lfos too in-your-face, sound effect only.
  > sweep_slow_ramp + phase_swirl least useful.
  All requested variants in run-9 batch (expand round 3).
- UI curve editor: good except vla_partials.bandwidth1 missing from the
  Add-curve dropdown (fix in progress, run 9); folder picker good;
  velocity bug unreproducible-and-gone.
- Second instrument: Bb clarinet chosen ("sounds wonderful"; trumpet
  samples disappointing — no attack character, skipped). Matt's
  observations logged: wind/brass attack speed & breath amount inversely
  proportional to frequency; these samples = gold standard for the
  breathy attack we've failed at repeatedly. My call per his question:
  ATTACK ANALYSIS FIRST (run-9 agent), then CMA-ES — a viola-shaped
  encoder won't invent breath structure, and run-7 already flagged
  scorer-vs-ear divergence on structural terms.
- FM alias A/B: inaudible as rendered — regenerating per his spec (5 Hz
  freq LFO to expose aliasing, notes C4-C8, os1 vs os8).

2026-07-29: curve editor requested->landed; CMA-ES "no worse" -> stage e
approved; expand round-1 categorization (see git history for verbatim).

2026-07-28: v5 grounded adopted; frequency-dependence principle; v4/UI
verdicts. 2026-07-27: v3 verdicts.
