# 2026-08-24 — Dipsy: bow family round 1 + Listen-here fix (noise day 2)

Interactive with Matt ("Noise day 2, I'll allow it"). Two docket items done,
one gate-hygiene find en route.

## 1. THE BOW gets a family — 30 cells (REVIEW 50)

`tools/gen_bow_family.py` off the exact discovery bytes
(patches/baselines/bow_evolution_discovery.json), one axis moved per cell,
notes 36/60/84, auto-leveled. Axes = Matt's 08-23 parameter notes verbatim:
RedNoise frequency ladder + a **pitch-tracked** cell (rnfreq_track — the
accidental 350 Hz is register-blind; the stochastic-bow reading wants events
near the string period), density ("hesitant"), smoothness / continuity /
rampVariation, boost + zeroCrossTendency (the TBDs), bowPosition, friction,
tubeLoss, three combo characters. Output: patches/audition/bow_family/ +
renders/dsp/pending/bow_family/. All 30 non-silent, pitch-locked at all
three registers (spectral peaks on f0 harmonics throughout).

**Parameter-space finding:** bowSpeed × frictionGain is ONE knob. The
Friedlander recursion obeys S(λ·speed, gain) = λ·S(speed, λ·gain) exactly,
so waveform shape depends only on the product ("drive") and the residue is
pure gain — verified corr=1.0 between rendered pairs. The two bowspeed
cells were exact duplicates of the fric cells and were dropped (patches
moved to patches/old/bow_family/). Family design implication: expose one
"drive" control, not two.

## 2. Backlog 32 — Listen-here silent on non-string nodes (REVIEW 51)

Reproduced Matt's report in the CLI by re-rooting creak__wtsaw15_bedlow at
each node. Two separate causes:

**(a) True zero on wt — starved RefSource.** resolve_param wraps a shared
source's SECOND+ consumer in RefSource; only the FIRST-WIRED consumer
advances it. A Listen tap re-roots graph.output, and when the advancing
consumer falls outside the tap's render cone the source never advances —
bow_env (advanced by bed, wired before wt) froze at 0.0 and muted wt's
amplitude. Fix in patch_loader.cpp: `promote_starved_refs`, run after each
graph build. Starvation is proven from the patch JSON (the advancing node —
first in wiring order to mention the ref — is unreachable from the output),
then the built cone is walked and only proven-starved RefSources are
promoted to advancing. First cut decided starvation from the built graph
and FALSELY promoted through non-enumerable pins (additive attack-envelope
lists have no getter) — caught because it would have looked like it changed
56 voice patches (it hadn't — see §3 — but the JSON-proof design is the
correct one and shipped).

**(b) "Silent" att/bed/sums — instrument volume.** They rendered fine, just
20–30 dB down: instrument.volume is auto-leveled for the string's resonant
output, which a raw excitation tap never went through. Matt's call: Listen
taps now monitor at unity volume (save_patch_graph tapOverride path).

Validation: wt tap now sounds (matches the exclusive-ref control render
exactly); full-graph renders byte-identical (null gate below); both
mforce_cli and mforce_ui rebuilt.

## 3. En route — null-gate manifest was stale

First full gate run showed 56 DIFFs, all patches/library/voice/. Bisect
(engine path-checkouts back to P3) proved NO engine commit caused them: the
08-22 voice release-bump (8657bae, release 0.0→0.1 on all 58 voice
envelopes) changed the patches themselves after the manifest froze at P1,
and the same-day curation left 87 GONE entries. Nobody re-froze. Manifest
re-frozen with the fixed build: **180 entries, 180/180 identical.**
Standing lesson: a deliberate patch edit or curation pass must re-freeze
the manifest the same day, or the next engine change gets blamed for it.

## 4. Same-day addendum — Matt's headphone sizzle, measured and split

Matt: great through Bose speakers; on headphones "noise zithering"/sizzle,
mid noticeable, high pronounced, ALL sweep cells. Measured (harmonic-to-
inter-harmonic-floor per band, sustain windows): floor at or ABOVE harmonic
energy >2 kHz in every cell. Isolation experiments:

- Constant bow pressure (no RedNoise): notes 60/84 clean up 14-28 dB;
  note 36 unchanged → two mechanisms suspected.
- Vibrato depth 0: note 36 floor collapses ~25-30 dB → the DOMINANT
  mechanism is the KS-bend fractional read head (Approach A) against a
  LIVE evolution — reader drifts past writer, output sweeps the
  old-pass/new-pass seam of the string state. Backlog 33 (needs a real
  vibrato mechanism for waveguide tables — direction is Matt's call,
  Approach B was parked deliberately).
- The secondary mechanism (bow noise recirculating with only flat
  tubeLoss) got the structural fix Matt approved: **brightness** on
  BowedStringEvolution — one-pole per line write, default 1.0 bypasses
  entirely (discovery render byte-identical, hash-checked). With vibrato
  off, brt080 lifts note-60 hi-band 8.3 → 18.2 dB.

New family cells: brt095/090/080/065/050 ladder, vib000, vib000_brt080
(both fixes — the diagnosis demo). REVIEW 50 updated with the verdict asks.
Lesson relearned the hard way: isolate mechanisms BEFORE building the fix —
brightness was built on the recirculation theory while the bigger cause was
the resample seam; it survives as a legitimate tone control, but the order
was wrong.

## Files

- engine/src/patch_loader.cpp — promote_starved_refs + 5 call sites
- tools/mforce_ui/main.cpp — tap monitor at unity volume
- tools/gen_bow_family.py — sweep generator (documents the drive symmetry)
- tools/null_gate_manifest.json — re-frozen, 196 → 180 entries
- patches/audition/bow_family/ (30) + renders/dsp/pending/bow_family/ (30)
- Matt's own uncommitted piano work (piano_default tone-curve halving +
  bright/dark variants) left untouched in patches/library/keys/.
