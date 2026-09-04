# Feedback-loop sweep r4 — Matt's verdicts (2026-09-02)

Round: 24 slow-bloom corners (20 r3 picks + 4 nature-junction archetypes),
critical-relative drive (+3% above bisection-measured critical, 400 ms ramp).
Queue was renders/dsp/pending/feedback_curves4/; full 216 grid in
patches/sweep/feedback_curves4/. Matt's saves (some hand-tweaked in UI):
**patches/scratch/loop_sweep_4/** — those files, not the sweep cells, are the
verdicted artifacts.

## Overall

> "You nailed it. Concept proven."

The r3 painstaking 100-cell review paid off as the source of the 20
in-the-ballpark starting points. Two patches library-grade.

## By family

- **Winds best** — legit flute and reed sounds. Some brassy in character,
  but legit *brass* needs more work.
- **Brass (lip family)** less successful than the reed/string variants that
  came out brassy.
- **Strings least successful** — best ones don't approach the additive synth
  patches. Most sound brassy or reedy due to absence of the elusive bow
  attack. No bows yet.
- Best patches sound characteristic in their *home* register only, not
  below/above it.

## Per patch (names = scratch/loop_sweep_4/ files)

| Patch | Verdict |
|---|---|
| brass001 | Character not bad; audible click on attack (excite segment) then too-brief swell; at higher freqs swell absent, attack instantaneous |
| flute001 | Not bad; attack not quite right — perceived percussive hit when tube becomes "fully loaded" |
| **flute002** | **KILLER. Promoted → patches/library/winds/flute_default.json** (byte-copy, 09-02). Attack fully integrated by the physical model: appropriate breath + subtle squeak, no percussive arrival |
| flute003 | Also good; less noisy/squeaky attack than flute002 |
| odd001 | Two-part attack: light pluck then screechy swell |
| odd002 | Tuba/trombone character low; higher notes boring synth with attack tick |
| odd003 | Clear tone, zithery/fluttery/mothwing attack |
| odd004 | Lower-freq fluttery attack, chimy sustain |
| **reed001 / reed001a** | **KILLER** — integrated attack "makes Clarinet1 sound fake"; instrument identity unclear (oboe?). reed001a = Matt's tweak, keytrack term → 3 (more oboe-y); at that setting some notes overblow more than others, not every time (see physics note) |
| reed002 | Character has promise, physics not quite right: percussive arrival like flute001 + lots of overblowing even/especially at low cutoff |
| reed003 | Pretty killer, esp. lower register; noise needs dialing back at higher registers (likely true of most flute/reed patches here) |
| reed004 | Was a brass variant, sounds reed: nice attack, honky, squeaky/overtoney — varies across hits of the same note (real, see physics note) |
| reed005 | Another good one |
| reed006 | Weaker — attack NOT integrated, reads as Clarinet1-style noise+tone (though tapping the key gives *tuned* hiss). Possibly too far above critical |
| string001 | Was string variant; sounds like a not-as-good reed |
| string002 | Different, but ditto |

## Physics notes (code-verified 09-02)

Matt's Q on reed001a: "sounds like energy left in the tube causes the
overblow… but they're separate pooled voices so any interaction is just
in my brain, rite?"

- **No tube memory**: DelayLine::prepare zero-fills the buffer
  (delay_line_source.h:85) and SVFSource::prepare clears ic1eq/ic2eq/lp1
  (svf_source.h:93-95) at every note-on, and reed001a is polyphony 1 anyway
  (same graph every note, wiped each time).
- **But per-hit variation is REAL**: WhiteNoiseSource::prepare does NOT
  re-seed — the RNG free-runs across notes (white_noise_source.h:67-73).
  Every attack gets a fresh noise realization, and at +3%-above-critical
  the loop is marginal: whether/when it tips into overblow depends on the
  noise instance. So "not every time" is genuine non-determinism, not
  imagination — there's just no *energy* carried over, only RNG phase.
  Same mechanism = reed004's hit-to-hit squeak variation ("pretty cool").
- reed001a's per-*note* overblow spread: cutoff = 3×f0 keeps the fc/f0
  ratio constant, but absolute-frequency effects (Nyquist warping of the
  SVF response, DC-blocker interaction, drive calibrated per-cell at the
  *original* cutoff, not re-measured after the tweak) move the effective
  loop gain per note — some notes sit further above critical than others.

## Register problem (cross-cutting)

Three of Matt's points are one issue — parameters tuned at the home note
don't scale across the keyboard: brass001's swell vanishes up high,
reed003's (and most winds') noise too loud up high, everything
characteristic only in home register. The keytrack/expressions machinery
that fixed cutoff (37a-a) exists; noise amplitude and drive-ramp time are
not yet keytracked.

## Status

- flute002 → library/winds/flute_default.json + renders/library/
  flute_default.wav (archive render, peak 0.17). clarinet1's "default wind"
  role now shared.
- reed001a library-ready by ear but unnamed (oboe?) — awaiting Matt's call
  on name/family before promotion. → RESOLVED 09-03: promoted as
  **patches/library/winds/oboe_default.json**, Matt's own build-out from
  the r5b material: keytrack up to a=6, breath-noise curve, Vibrato node,
  fixed ~1.1 kHz formant (loose SVF bandpass + Sum bell — the honk),
  Reverb on the output. Archive render renders/library/oboe_default.wav.
- r5 steer: from these verdicts, not yet scoped. → r5a (keytrack round)
  and r5b (reed001a deep-dive) both ran; see backlog 37.
