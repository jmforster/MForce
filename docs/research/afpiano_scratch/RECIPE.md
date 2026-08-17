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

## Still to read (dense-frame pass when needed)
- brightness/resonance knob value (range 1.01-12, transcript);
  per-string allpass FB exact trio (0.1 / 0.2 / 0.125? transcript) and
  per-string damping cutoffs; env-follower attack knobs (~9-10 ms +
  curve mod); post SVF cutoff knobs + low-cut; velocity crossfade
  endpoints (12 / 64, transcript); damper-noise layer LP + strength.
