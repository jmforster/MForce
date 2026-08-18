# AF saxophone patch — complete value table (video archaeology)

Source: https://www.youtube.com/watch?v=jTgs9qPkGQU ("breakdown", 22.7 min,
2560x1440 — every knob legible; downloaded af_sax_hd.mp4 + transcript).
Played-demo reference: https://www.youtube.com/watch?v=B1GGNz2iylc
(af_sax_demo.wav). Balazs: "basically the same patch" as the demo.
The patch file itself is downloadable from the AF Discord (best source;
Matt joining). Frames in frames/z_*.png.

## Architecture (all values frame-read; no guesses remain except the
## three marked TUNE)

MidiNote(P,V,G) -> Bend -> Legato -> Vibrato -> [pitch bus]
ADSR "air pressure" -> SVF (self-osc sine source) -> Air -> Resonator
-> Body (tiny reverb) -> out (-11.86 dB master).
Expression (VST param) -> Power -> Resonator loop gain.

### 1. ADSR Modulated ("air pressure")
Attack 10.0 ms, Decay 731.2 ms, Sustain 0.19, Release 6.2 ms.
Feeds: SVF audio In, Air.E, Power.E.

### 2. Tone source = SVF driven to self-oscillation
Filter SVF Modulated: Cutoff 0 + CM = final pitch bus (MIDI units),
Q base 0.00, KBD 0, slope 12 dB/oct (slider at 12 end).
RM (resonance mod) <- Re-scaler(gate): gate 0 -> 0.1, gate 1 -> 0.00x
(AF SVF self-oscillates as res -> 0; keyed = singing sine at pitch,
released = damped). Output tap to Air: the LP tap (transcript "P pass
output"). Our engine cannot self-osc the SVF -> approximate: sine osc
at pitch x ADSR (loses the release "click" transition; acceptable v1).

### 3. Pitch bus
- Bend: pitch + 2*bend (+-2 semitones).
- Legato: env follower ON PITCH; follow time = CustomFunction(velocity)
  600 ms (v=0) -> 15 ms (v=1), concave; multiplied by Previous(gate) so
  the FIRST note glides not at all (prev gate 0 -> time 0) and overlapped
  notes glide at the velocity-scaled time. (Portamento only on legato.)
- Vibrato: TRI LFO 5.8 Hz (Inv on), FM'd by RND LFO 1.3 Hz (Inv);
  bipolar (2x-1); x env follower on gate, attack 6000 ms (6 s fade-in);
  added to pitch in semitones. Depth TUNE (unit amplitude suggests
  +-1 semi at full swell; scale to taste ~0.3-0.5).

### 4. Air (breath) — multiplicative
y = x * (1 + 0.1 * whitenoise * ADSR.E). Confirmed nodes: Mul1(noise,E)
-> Mul2(x0.1) -> Add(+1) -> Mul(x). Breath rides the pressure envelope
INSIDE the signal (no parallel bed).

### 5. Resonator ("KS on a sinusoid" — THE character)
Forward: x -> TanH(drive C=1) -> Add -> y.
Loop: Add -> Delay -> Mul(x pow) -> Filter 1P Modulated (damping,
Cutoff 35.10 MIDI + CM = pitch bus; ~2 kHz at C4) -> TanH(C=1) ->
Add.B (join).
Delay time D = 2 * pitch2ms(pitch) (doubled via self-Add), MODULATED
audio-rate: D_eff = D * (1 + 0.36 * tanh_forward_output) (Gain Lin Vol
0.36 -> +1 -> Mul with D). This audio-rate delay-mod is "the trick" —
the brassy/reedy roughness.
Loop gain pow = Crossfade(A=0.5, B=1.16, X=Power.pow) — >1 with tanh
saturation = sustained reed; low expression = dark dying tone.

### 6. Power (expression -> scoop)
pow = EnvFollowerModulated(In = Expression * ADSR.E,
  attack = CustomFunction(velocity): 600 ms (v=0) -> 16 ms (v=1),
  attack base 0.0 ms, release 42.2 ms).
Soft/low-expression notes swell the loop gain over up to 600 ms = the
velocity-sensitive "scoop"; hard notes speak in ~16 ms.
Expression = VST parameter #1, range 0-1, shape 1 (linear), value knob
seen at 0.33/0.87 during play.

### 7. Body = tiny Reverb (the "magic" from his string video)
Stock Reverb node: Predelay 0.00, Size 0.06, Decay 0.39, Lp 8408 Hz,
Dry/Wet 1.00 (100% wet in the x -> body path; body replaces direct).
Main out: Stereo Track fader -11.86 dB (final; -12.21 seen earlier).

## TUNE list (the only unreads)
- Vibrato depth scaling (LFO output gain knob unread; start +-0.4 semi).
- SVF output tap assumed OLP.
- TanH drive shape = AF's tanh node (ours would be plain tanh).

## Engine gaps for a rebuild
1. Self-oscillating SVF source -> substitute SineSource x ADSR (v1).
2. Resonator: needs a feedback delay loop with (a) audio-rate delay-time
   modulation, (b) in-loop 1P damping, (c) in-loop tanh, (d) loop gain
   >1 (saturating). KSPianoString has none of (a)(c)(d) semantics ->
   NEW NODE proposal: ModDelayLoop (delay w/ mod input, fb path with
   tanh + 1P, gain input). Discuss before building.
3. Tiny-reverb body: no reverb node in engine. Options: 4-comb mini
   Schroeder node, or approximate with existing allpass/comb chain, or
   skip body v1 (he demos pre-body sound as "usable").
4. Expression pedal: map to a MIDI CC / constant for offline renders;
   velocity can stand in when no pedal data.
