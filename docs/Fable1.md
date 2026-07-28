General guidelines:
This first task is in part a test of working more independently, with me out of the loop.
You may proceed write specs and plans but always proceed to implementation.
You have permission to update any settings (dangerously_ignore_warning etc.) to avoid interruption.
You may ignore or make use of the "superpower" skills at your discretion.
You may implement and build new code at will.

Scope:
We will stick to the dsp thread of effort for now.
DSP brother was working on creating a "credible viola" patch.

Background:
The partial spectrum and formant are the easy part, the hard parts are:
1. Noisy attack that doesn't sound like crossfaded noise
2. Inharmonic partials "smearing" the spectrum vs. clean spikes
3. The constant movement of those partials and the *evolution of that movement* over time

It's my opinion/theory that Additive synth has been less explored due to computing constraints.
With no limit on the number of partials and the ability to have them shrink, grow, move, "smear", trade places, whatever... the sonic possibilities are literally endless.
Some of these possibilities are unexplored, and we need to explore them.
I am not concerned for now with the time it takes to generate the audio (ie, does not have to be realtime streamable).

Current state:
My last checkpoint with DSP brother was (*** marks start and end):

***
We're not close to credible, vibrato is nice but the base sound lacking. So for the "broadening" - and potentially for the holy grail of a noisy attack that isn't KS - we have the following Additive features:

1. Rolloff - ramping from 0 to target value would theoretically produce a "buzz" morphing into instrument sound
2. Detune - partials could ramp from "all over the place" to their specified positions - have tried this and it just sounds like what it is - a bunch of clashing notes moving to harmonious notes - ie the independent partial movement makes the ear perceive each partial as its own note
3. PartialGroups - this can handle the broadening, and potentially ramping group width / accuracy from wide and inaccurate to narrow and accurate *could* produce an order out of chaos attack effect
4. PhaseOffset - effect probably too subtle to help, but not sure
5. Any of the above can obviously be modulated vs. ramped if there is any potential there (say, a rapidly oscillating unit PO that then "settles down"
***

In response to the above, DSP brother implemented "bandwidth enhanced sinusoidal modeling" as the latest attempt to tackle these challenges, with disappointing results. 

Your task:
Do *anything you want* with *any number of partials* - using the existing Additive features *or building new ones* to achieve 2 goals:
1. A chaotic attack that settles into its sustained timbre
2. A continued movement of partials over the sustain phase to produce a credible, evolving, "shimmering" timbre

Latest patch is C:\@dev\repos\mforce\research\ml_ears\patches\viola_instrument.json
Feel free to use that as a starting point, or start over.


Use any metrics you want to confirm the renders contain audio, are not steady-state, etc., but for now, don't bother with ground truth / evaluation.

I understand that you won't know whether the results are "credible" or "shimmering" etc., the point is for you to try some things that haven't been tried.

Output:
10 patches
10 renders of a C major scale in octave 4.
A summary of code changes / features you added.
A table listing the renders and what techniques you used to produce them.


