# AFNoding-031 piano — from-scratch rebuild recipe

Source: https://www.youtube.com/watch?v=vLf1r1uqTg4 ("Noding session 31",
~40 min, Balázs rebuilds the piano patch node by node with narration).
Downloaded 2026-08-16 (afp_scratch.mp4, 1080p60) + auto-captions
(transcript_raw.txt — auto-caption errors; values below cross-checked
against frames where marked [frame]). THIS is the patch our v5/v6 line
was chasing; it is SIMPLER than both the 2021 predecessor and our patch.

## Signal flow (narrated skeleton; values being filled from frames)

### 1. Note plumbing
- MIDI note -> pitch (MIDI-note units, "0-150" ranges throughout).
- Gate -> two ENVELOPE FOLLOWERS (attack ~9-10 ms, CURVED — "the attack
  is not a straight line"; attack-time modulated). One for the main hit,
  one reserved for the "reverb"/damper layer.
- Velocity multiplies the envelope output ("piano is velocity dependent").

### 2. THE key invention — pitch limiting ("limit P", 4:24-6:00, 34:12)
    limited_pitch = min(pitch + 12, stiffness)
- stiffness is a knob in MIDI-note units, range 0-128, set ~60-80
  ("the value was 60 80" [check frame]).
- Every excitation/post filter cutoff that "tracks pitch" tracks THIS.
- His own words: "this was the key that I didn't find — I was always
  trying to create extreme curves and cutoff resonance settings and it
  was completely wrong." Demo at 34:12: stiffness high = harsh, not a
  piano; limited = piano.

### 3. Excitation (2:16-8:40) — NO bank, NO click path
    white noise
      x envelope follower (gated, ~10 ms curved attack)
      x velocity
      -> SVF LOWPASS, cutoff = limited_pitch,
         LP output DIVIDED by resonance; resonance 1.01-12 = the
         "brightness" parameter
      -> x6 (level)
      -> ONE-POLE lowpass, cutoff = limited_pitch
      -> strings (same signal to all three)
"It's simply a white noise filtered... nothing too special."

### 4. String x3 (8:44-20:30) — KS loop, per-string flavor
    in Add -> Delay(pitch2ms(pitch) - 1 sample)
           -> ALLPASS (delay-type allpass, same delay time, feedback
              0.1 / 0.2 / 0.125 per string [check])
           -> Mul (feedback gain) = crossfade on GATE:
                gate held  -> 0.999
                gate off   -> low value (release), SMOOTHED by an
                envelope follower ("or we can get clicks")
           -> dampening filter (SVF LP, cutoff knob 0-150 region,
              per-string values differ)
           -> out (the LP is the output tap)
- Tuning: delay -= 1 sample (the loop's own sample of latency);
  pitch2ms node does the conversion; sample length = 1000/sr ms.
- +12 pitch compensation: "we are one octave lower than normal because
  of the allpasses in the feedback."
- Detune: DT1 knob, "0.001 ... 24" [check]: added per-string to the
  delay time (string 2/3 offsets).
- Per-string differences are DELIBERATE taste: different allpass
  feedback, damping, release.

### 5. Post-string chain (21:26-25:30) — what our patch lacks entirely
    Add(3 strings)
      -> SVF #1: HIGH-PASS output; cutoff = low-cut knob + limited
         pitch [check]; resonance 3
      -> SVF #2: LOW-PASS output / resonance; resonance 8;
         cutoff = VELOCITY crossfade 12 -> 64 (+ limited pitch? [check])
         ("to have the ivory whispering" — soft = dark, hard = bright)
      -> ONE-POLE filter, cutoff = pitch (or stiff [check]) — final stage

### 6. "Fake reverb" / damper noise layer (25:33-30:19)
- Second envelope follower (same gate/trigger, own attack+extra attack),
  x velocity, x strength knob -> SECOND white noise (own LP shaping)
  ADDED to the main noise ("we can shape the frequency spectrum of this
  reverb... it is a damper").

### 7. Sustain pedal (30:40-33:30)
- CC64 > 0.5 latch, crossfades the string feedback's gate so release
  stays long while pedal held. (We have damper/pedal machinery; low
  priority.)

## Mapping to our engine (implementation plan)
1. NEW ENGINE NODE: SVFSource — 2-pole state-variable filter, params
   cutoff (+res), outputs LP/HP/BP selectable, optional LP/res
   normalization (his "divide by resonance"). Justified: his patch uses
   SVFs in four places; our BW filters have no resonance and no HP tap.
2. limited_pitch: paramMap curve does min(pitch+12, stiffness) exactly —
   a frequency curve that is linear then FLAT (the clamp IS our explicit
   endpoint convention). No engine work.
3. Excitation chain: nodes we have + SVFSource.
4. String: keep KSPianoString (loop structure equivalent: delay + damping
   + APs + gate-crossfaded feedback = releaseFb); revisit only if the
   A/B against his renders says the loop is the gap.
5. Post chain: SVFSource HP -> SVFSource LP/res (velocity vcurve on
   cutoff) -> 1P. New patch afp31.json built from scratch (Matt: "no
   attachment to our current patch").
6. Damper noise layer: second envelope + noise + LP — nodes we have.

## Values read from frames (2026-08-16 pass 1)
- **stiffness = 60.82** (knob range 0-128) [f_0120]
- **feedback crossfade: held B = 0.999, released A = 0.8**, smoothed by
  Env follower Modulated **attack 0.0 ms / release 210.2 ms** [f_0079] —
  the damper IS a 210-ms-smoothed feedback drop 0.999 -> 0.8.
- **Detune: CENTS mode, knob range 0.001-24**, String1 = pitch+detune,
  String2 = pitch-detune (Add/Sub pair), String(3) = pitch [f_0120]
- **x6 = literal Mul B=6** after the SVF/res division [f_0120]
- String loop order confirmed: in-Add -> Delay(pitch2ms - 1 sample via
  SampleLen) -> Allpass(FB per string) -> Mul(feedback) -> Filter 1P
  (dampening, P-tracked) -> out [f_0079]
- Original's own section labels [f_0120 right panel]: "Hammer sound
  shaping"; "1pole LP — reducing the amplitude of higher frequency
  strings"; "Body — post filter stage: removes unneeded lows and highs";
  "3 Strings"; "Velocity dependent LP filtering — the lower the
  velocity the darker the sound"; "Low cut".

## Values read from frames (2026-08-17 pass 2 — Matt's v2 verdict pass)
- **AF units are PLAIN MIDI** — z_velxfade: the velocity crossfade
  endpoints A=12 / B=64 are ADDED to limited pitch (Add node), which
  only lands sanely (587 Hz .. 12.5 kHz) in plain MIDI. The earlier x6
  "calibration" conflated the 2021 patch's 4P output knee with this
  patch's excitation cutoff. Stiffness 60.82 = ~277 Hz, literally.
- **Hit envelope** — z_envfollow: Trg->Gate Time = 10 ms (a PULSE) into
  "Hit length" env follower attack 0.0 ms / release 361 ms; so the hit
  is INSTANT attack, 10 ms hold, ~exp 361 ms release. The "Reverb
  length" follower: attack ~15 ms / release 462.2 ms (matches 2021).
- **Post-chain resonances confirmed**: SVF1 (Body HP) res 3, SVF2
  (velocity LP) res 8 with divide-by-res.
- v3/v3b consequence: faithful 277 Hz clamp leaves C6 ~-48 dB into the
  string (his video never demos the top octaves) — v3 is faithful, v3b
  floors the excitation cutoff at f0 so treble stays alive.

## Values read from frames (2026-08-17 pass 3 — "any other guesses?")
- **Excitation SVF cutoff is FIXED, not pitch-tracked** [z_brightness]:
  a separate Brightness knob = 91.50 (0-150 MIDI) ~= 1614 Hz feeds the
  SVF cutoff; Resonance knob (range 0.001-12) sits low ~1.2 and feeds
  both the SVF R and the Div. B2 = 6 = the x6. Only the Filter 1P
  receives the limited pitch min(pitch+12, 60.82).
- **Post chain** [z_postsvf, z_detune_2130]: Low cut knob = 3.30
  (range 0-12, semitones) added to limited pitch -> Body HP (Filter
  2P 1, R 1 = 3, H tap); then Filter 2P 2 (R 2 = 8, L tap) -> Div by
  res; then a FINAL Filter 1P fed the plain (+12-compensated) pitch.
  Velocity crossfade A=12 / B=64 confirmed on-screen.
- **String internals** [z_string_1940, z_string_2010, z_damp_1325]:
  original String 1 held feedback = **0.9995** (not 0.999) -> t60 =
  13800/f; released = 0.8; follower 0.0 ms / 210.2 ms confirmed.
  Allpass FB: String 1 = 0.1, String 2 = 0.2. In-loop damping 1P
  cutoff = gate-follower x knob **135.00** (0-150 MIDI ~= 20 kHz):
  nearly OPEN while held, slides to 0 over 210 ms on release — the
  damping filter IS the damper, brightness is flat, not pitch-tracked.
  Detune knob (cents, 0.001-24) read 0.00/~0.38 mid-build — his final
  value still unread.

## Values read from frames (2026-08-17 pass 4 — reverb layer)
- **"Inner reverberation"** [z_rev_2640/2740/2850] — original's label:
  "The sound is reflecting inside the body. This is emulated by a long
  releasing hammer noise on a lower volume than the main hit."
  A SECOND White Noise node x its own env follower x velocity x
  Strength knob, SUMMED with the hit noise before the excitation
  filter stack. Original follower: attack ~16.9 ms / release 462.2 ms
  (matches the 2021 patch). His rebuild's knobs read 36.0/702.2 ms
  mid-set — the original values are authoritative. Trg->Gate on this
  path = 9.0 ms; hit-attack Time node = 10.0 ms. Strength knob ~0.17
  on-screen (exact value unread). Optionally its own shaping LP
  ("we can also use a different noise... shape with a low-pass");
  simplest faithful form shares the main excitation SVF.
  IMPLEMENTED in afp31_v5 (REV_ENV + REV_STRENGTH in gen_afp31.py).

## Still to read / genuinely unreadable
- final detune cents (knob read 0.00 then ~0.38 mid-build; never shown
  settled); exact Reverb strength value; per-string damping-cutoff
  differences (if any). Sustain node = MIDI pedal passthrough, not
  needed.

## GROUND TRUTH (2026-08-18): the original patch file, decoded
Matt recovered the dead patch page via Wayback Machine. The download
(docs/research/afpiano/patch.txt, base64 AF clipboard format) is fully
decoded — every node, knob, and wire — in
docs/research/afpiano/parse_full.txt. The FILE supersedes all frame
reads. Corrections vs our frame-era beliefs:
- FINAL FILTER IS A ONE-POLE HIGHPASS at pitch+12 (2*f0), H tap — we
  had a lowpass (darkening; his thins lows).
- Post-SVF gain is x2, not x6 (const B = 2).
- Excitation SVF: cutoff Brightness 91.405 MIDI ~= 1605 Hz, res 8.46
  (normalized /res) — not 1.2.
- Hit follower release 144 ms (not 361); Trg->Gate 9 ms; reverb
  follower release 1444 ms (not 462).
- Reverb noise band-passed: own 1P LP at 94.5 MIDI (~1917 Hz) then 1P
  HP at 62.25 MIDI (~298 Hz), x velocity x Strength 0.155, summed with
  hit noise BEFORE the excitation SVF.
- Strings: allpass FB 0 / 0.1 / 0.2; damping 1P FIXED (no gate mod) at
  135.2 / 130.1 / 125.3 MIDI (20.1k / 15.0k / 11.4 kHz); held fb
  0.999 / 0.995 / 0.999 (t60 = 6900/f, middle string 5x faster =
  built-in double decay); released fb 0.8; smoothing follower 196 ms.
  The video's env-modulated damping was his REBUILD, not the original.
- Detune knob ~0.0013 cents == effectively zero (curve-4 knob at
  norm 0.06); unison character comes from FB/damping differences.
- Stf 60.16 MIDI ~= 264 Hz; Low cut = 0; Body HP res 3 at
  min(2f0, 264); velocity LP res 8 /res, cutoff = limited + 12..64
  semis (linear in SEMITONES); master Gain 0.5.
- Sustain: CC64 latch crossfades string G, 0.5 threshold.
Faithful rebuild: tools/gen_afp31_gt.py -> afp31_gt.json/wav (taste
layers excluded; engine-gap approximations listed in its docstring).
