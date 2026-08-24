# One-shot segment sweep — round-1 verdicts (Matt, 2026-08-23)

Working log for the area-by-area audition discussion. Each area gets: Matt's
comments (verbatim where nuance matters), the cells referenced, and distilled
round-2 implications. Round 2 gets designed from this file once all areas are
in. Sweep artifacts: renders/dsp/pending/segment_sweep/ (REVIEW 43),
generator tools/gen_segment_sweep.py.

## Overall

- "Many are promising."
- **Try some longer durations** — round 1 capped at ~0.6-1.1 s (dots/scrapes)
  and much shorter for atoms/clusters; round 2 should ladder duration up.

## Areas

(recorded as Matt gives them; Matt listened to ALL cells in the family
folders, not just the _picks.)

### physics / bounce (4 cells: bounce_slow, bounce_fast, bounce_fast_shrink, bounce_soft_tri)

Verdict: **"nice, subtle"** — keep, expand.
Round-2 asks, verbatim: "try some longer durations, chunkier bounces, and a
reverse bounce."
Distilled:
- **Longer**: slower interval decay (r_int closer to 1), higher max_n, larger
  initial interval — bounces that take 1.5-3 s to settle.
- **Chunkier**: heavier atoms — wider (4-15 ms), softer shapes (tri/sine),
  maybe bipolar, lower centroid; possibly a two-component bounce atom
  (thump + tick) so each impact has body.
- **Reverse bounce**: time-reversed recurrence — intervals and peaks GROW
  (a settle played backwards: accelerando in reverse, rare-then-building);
  also worth a variant reversing peaks only (quiet hits speeding up into a
  big final hit — the lead-in idea wearing physics clothes).

### physics / scrape (12 cells)

Verdict: **"mostly unsatisfyingly white-noise-y"** — Matt asked for the
mechanism rather than a re-spin.
Diagnosis (structural, not perceptual): round-1 scrapes are trains of
INDEPENDENT micro-events — ~0.2-2 ms t^1.2 ramps with instant drop, intervals
drawn fresh per event (exp/gamma/jittered around 40-600/s), amplitudes
randomized, signs alternating/random. Independent near-impulses at random
times = shot noise ~= velvet noise with jitter; the whiteness is the
construction, not a rendering accident. Nothing couples one slip to the next.
Round-2 candidate mechanisms (offered, awaiting Matt's pick):
1. slip-interval coupling (relaxation dynamic: big slip -> longer re-stick ->
   bigger next slip) — one line of state, kills the Poisson character;
2. surface walk — slip width/peak follow a slow random walk so neighbours
   resemble each other (RedNoise continuity at the EVENT level);
3. quasi-periodicity — scrape_jitter_regular (cv 0.14) was the only
   near-regular cell; more regularity + higher rate heads toward creak;
4. fatter slips (5-20 ms shaped builds instead of near-impulses).
Known limit recorded: waveform-only scrape has no resonance/memory — the
surface/body imprint belongs to the excitation phase, not round 2.

**Matt's round-2 picks (final for scrape):**
- **Fatter slips: yes — and "start narrow and *grow* fatter"** across the
  train (slip-width schedule within the event, thin ticks thickening into
  zips).
- **Shorter durations** for scrapes, contrary to the overall longer-durations
  note (round 1 ran 0.6-0.8 s).
- **scrape_jitter_regular** "has a buzzy character with a low freq rumble —
  some more of these": expand the regular-interval family (rate ladder,
  regularity ladder around cv~0.15, press/width variants).
- **scrape_then_thump is THE STANDOUT, "with potential for excitation
  payoff" — more of these**: scrape-into-terminal-event variants (different
  scrape characters into different thumps, thump timing/size ladder,
  maybe thump-then-scrape and scrape-thump-scrape).
- Mechanisms 1/2 (slip-interval coupling, surface walk) not explicitly
  picked — fold lightly into the above rather than as their own ladders.

### physics / drop_settle (1 cell)

Verdict: **"only 1 example.. might be promising but not sure"** — undecided,
under-sampled. Round 2: give it a real family so there is something to judge —
vary the bounce half (slow/fast, chunky per the bounce asks) x the settle half
(crunch density/length, fine vs coarse), the handoff overlap, and relative
levels; include a couple with the reverse-bounce lead-in once that exists.
Small ladder (6-8 cells), not a priority push.

### physics / crunch (4 cells)

Verdict: **"try coarse>fine and fine>coarse"** — grain-size TRAJECTORY within
one crunch: micro-atom width (and probably density with it) sweeping coarse
to fine across the event, and the reverse. Same schedule idea as the scrape
grow-fatter ask and the dots ramp schedules — becoming the round-2 theme:
the interesting axis is a parameter MOVING during the one-shot, not its
static value.

### atoms (36 cells)

Verdict: sub-ms cells are what they are ("you know what 1ms sounds like").
**tri_10ms and sharp_30ms/80ms "sound like kick drums — definitely usable"**
(unsurprising, and noted as usable anyway); sine = kick but softer, also no
surprise. Distilled: the wide-atom end is a legit kick/drum-hit seed —
round 2 can ladder the 10-80 ms band more finely (width x shape x power,
uni/bipolar) as a "kick corner", low effort. No exotic ask here.

### clusters (17 cells)

Verdict: **"zips to farts"**. Most promising farts: **6_slow_leadin_shrink
and 6_slow_tailoff_grow** — note both are the SCHEDULED cells (env one way,
width the other), reinforcing the moving-parameter theme. Round-2 asks:
- variations of those two (n, spacing, atom width/shape, schedule depths,
  env/width schedule combinations);
- **pair them with a thump**, same construction as scrape_then_thump
  ("2 for 1!") — cluster-into-thump / thump-into-cluster variants join the
  X-then-thump family.

### dots (12 cells)

Verdict: **wide variation sound-wise. fixed_fine_zct = "earsplitting"** (drop
that corner: fine fixed width + forced alternation). **The grow_ cells =
"video game laser weapon bursts"** — Matt notes the kinship with FM given a
precipitous frequency drop, the classic synth-kick recipe (the width schedule
IS a frequency sweep, so this is that recipe in the time domain).
**"All usable except the earsplitter — definitely for percussion, possibly
for excitation."** Round-2 asks:
- **shorter durations** (dots joins scrape in running counter to the overall
  longer-durations note — round 1 dots ran 0.25-0.6 s);
- **"the most drummy would be the first half of hump"** — i.e. the rising
  half of the width schedule alone (fast→slow, stop at the widest point =
  the frequency-drop half without the return). Make first-half-of-hump its
  own schedule (equivalently: grow with a shorter total and wider w1),
  laddered short.

### rulebreak (7 cells)

Verdict: **"these didn't pay off"** — family demoted. **hold_then_snap is the
best of the 4** [heard]; round 2: a few variations on the hold-then-snap
theme (hold length, hold level, ramp-vs-instant edges, double snap), plus
other ways to break rules if any suggest themselves. Small corner, not a
ladder.

### twohit (15 cells)

Verdict: **heel_toe_soft and thump_ka are the best.** Round 2: **longer
duration versions with varying widths, especially of the thumps and kas** —
wider/slower thump atoms, ka width ladder, longer gaps (this family follows
the overall longer-durations note).

---

### NEW family for round 2: jagged (Matt, 2026-08-23)

Verbatim: "for lead-in, tail-off, or both, random noise turns into a jagged
ramp to a peak, with the jaggedness decreasing to 0 on the way up (reverse
on the way down)." Construction: v = ramp + j*noise, ramp 0→peak (power-
curveable), jaggedness j: 1→0 rising (0→1 falling); variants = leg (in/out/
both) x noise character (per-sample white vs coarse connect-the-dots) x jag
depth x ramp power x duration.

## All areas in — round 2 designs from this file.

---

# Round-2 verdicts (Matt, 2026-08-23)

### jagged (9 cells)

**"Jagged paid off! Snare drum city!"** — the new family lands on first
listen, and lands as a THING: snares. First round-2 family verdicted; a
snare-corner expansion is the obvious round-3 candidate (leg lengths /
noise character / depth / power around whichever cells read most
snare-like) — awaiting the rest of the round-2 areas before designing.

**Follow-up (same session): the three jagged_in cells are the snare ones.**
Matt's snare recipe, verbatim: "pairing that with a hit (pulse) at the onset,
varying the pulse width from sharp to fat, varying the jagged tail from white
to coarse, and varying the speed of the tailoff should produce an infinite
variety of snares." → built immediately as the snare_corner mini-sweep
(gen_snare_corner.py): pulse {sharp 1.5 ms / mid 8 ms tri / fat 25 ms tri} ×
tail noise {white / coarse 1800 Hz / coarse 700 Hz} × tailoff {fast 0.10 /
mid 0.22 / slow 0.45 s}, tail = the jagged out-leg (body ramp fading under
emerging noise) with an overall fade so it actually tails off.

### bounce2 (8 cells) — partial verdict: the twocomp discovery

Matt asked what twocomp is (wide tri body + 0.8 ms full-peak spike
superimposed per impact). Verdict: **"it's quite good on its own, even if
not 'bouncing'"** — and on the mix-a-tick-with-a-thump alternative: "I'm all
about economy" — the superimposed single-shape version is the point.
Round-3 directives:
- **NEW family "combo": wide pulse with a spike superimposed**, a few
  combos — vary body width/shape, spike width/timing (at onset vs slightly
  in), spike-to-body ratio.
- **twohit gets REPLACED by *pairs* of combos** (twohit round 2 "not great
  so far") — heel-toe etc. built from combo atoms instead of plain pulses.
- **Reverse bounces "promising too"** — queued immediately (same day): a
  slightly faster (x0.6 timeline) and a lot faster (x0.3) version of each of
  the four reverse cells, rendered into segment_sweep2/bounce2/ as
  *_faster / *_fastest.

### clusters2 (8 cells)

Verdict: **"not that exciting as standalone noises"** — family de-prioritized
as a raw-sound front. **Noted for the excitation phase: both_shrink_then_grow
"might be an interesting excitation"** (12 tri atoms, rise-fall envelope,
widths shrinking then the schedule's grow tail). No round-3 cluster ladder;
the cell joins scrape_then_thump on the excitation-candidates list.

### dots2 / halfhump — follow-up

Matt: **"do a few at progressively lower starting 'frequencies'"** — the
halfhump cells all started at w0=1-2 samples (~white). Queued same day into
segment_sweep2/dots2/: halfhump_from{6000,1200,240,60}hz — starting dot rate
laddered down (w0 = 8/40/200/800 samples), same 1600-sample endpoint,
0.2 s, smoothness 0.5.

### scrape2 (11 cells + follow-up)

Verdict: **"still not doing it, not sure what else to try"** — second failed
round for raw scrape (round 1: white-noise-y; round 2's coupling / surface
walk / grow-fat / buzzy ladder didn't land either). Per the 2-attempt rule
this front PARKS as a standalone sound after the one last probe Matt asked
for: **"try reversing the growfats for fun"** — queued same day as
fatshrink_{020,035,press} (slips start fat, shrink to ticks) in
segment_sweep2/scrape2/. Standing note: the known missing ingredient is
resonance/memory (logged in round 1) — scrape's real prospect is the
EXCITATION phase (scrape_then_thump already the round-1 standout), not
raw playback.

### xthump (12 cells + follow-up)

Verdict: **thump_cluster_tailoff "quite promising"** — the only other
round-2 standout so far. Matt: "makes me wonder if a cluster->thump might
beat scrape->thump." Queued same day into segment_sweep2/xthump/: four more
cluster-thump combinations (cluster_dense_thump / _fatthump = denser
jittered lead-ins into small/fat thumps with tight handoffs;
shrinkgrow_thump = the excitation-flagged both_shrink_then_grow cluster
into a thump; cluster_accel_thump = two-stage accelerating lead-in). The
scrape->thump cells themselves await explicit verdict but the raw-scrape
parking (above) leans the family toward cluster-based lead-ins.

### snare_corner (27 cells)

Verdict: **"nothing good there"** ("mighta got out over my skis"). Two
diagnoses, verbatim: **"hits are not sharp enough"**, and **"what's missing
in tail is fast-decreasing *density* which may not fit the 'jagged' pattern
at all .. ie, maybe white > [pink/reddish] / dense > sparse."** So the snare
tail is a DENSITY structure (dots family machinery), not an amplitude/
jaggedness structure. Built same day as **snare_corner2** (16 cells,
gen'd inline; renders/dsp/pending/snare_corner2/): tail = impulse train with
geometrically growing gaps (dense→sparse; fastsparse r=1.16 / midsparse
r=1.09) and growing impulse widths (white→reddish), mild amplitude decay —
density collapse is the engine, not a fade. Hits: 0.7 ms spike, or combo
(spike + 8 ms body — the new combo atom's first outing). Grid:
{spike,combo} x {fastsparse,midsparse} x {0.12,0.25 s}.

### snare_corner2 (8 cells)

Verdict: **"those are terrible, so much for me designing sounds in my
head!"** — SNARES PARKED. Matt's own closing diagnosis, kept for the
return visit: **"the crack can't be a single pulse, that's only gonna give
a tick>thump spectrum.. and the snare tail stays at whatever frequency it
starts with, only density decreases"** — i.e. (a) the crack needs internal
structure, not one pulse of any width; (b) in the v2 tail the per-impulse
spectrum is static — density collapse alone doesn't redden anything, each
tick keeps its birth spectrum. Plan: **come back to snares after the noise
exploration finishes and instrument-roster building resumes** ("we'll steal
one from Balasz" — kidding on the record).

### bounce2 — final: re-based and PARKED for excitation

Matt: "overwrite all where the baseline = faster and fastest is faster ..
then park for excitation experiments." Done: the whole family regenerated
in place — every cell's baseline is the old x0.6 timeline, _faster = old
x0.3, _fastest = new x0.15 (24 cells, 8 shapes x 3 speeds, same folder).
**bounce2 PARKED as a raw-sound front; joins the excitation-candidates
list** (with both_shrink_then_grow and scrape_then_thump).

### dots2 — verdict

**Nuked: halfhump_boost_zct ("zct laser weapon") and halfhump_from60hz**
(files deleted; regenerable from the generator seeds). **"All others are
pretty good for percussion and potential excitation"** — folder KEPT with
existing examples. Added same day: five grow_250ms_boost variations (wide
end, deeper boost, shorter, longer+sine, sparse), four halfhump_from6000hz
variations (longer, wider end, from3000, sine), and **grow_3s_s0 — the very
long (3 s) stepped version of grow_150ms_s0**. dots2 joins the
excitation-candidates list.

### twohit2 (9 cells)

Verdict: **"none good .. all really quiet and the thumps in the thump_kas
are more like rumbles."** Diagnosis (mechanical, mine): the width ladder
over-widened — an 80-150 ms unipolar tri is a ~3-6 Hz half-period, mostly
sub-audio, so the cells are quiet and the "thump" reads as rumble. Lesson
recorded for combo design: **bare-pulse bodies live in ~8-50 ms; wider
needs a damped low-frequency cycle (a boom), not a fatter bump.**
twohit2 RETIRES without a fix — round 3 replaces twohit with pairs of
combos per the standing directive, with body widths capped accordingly.

### scrape2 — CLOSED (raw front)

The fatshrink_{020,035,press} reversal probe was rendered into the folder;
Matt: **"still not good... grail remains elusive."** Scrape closes as a raw-
sound front: two rounds + one probe, nothing landed. Standing conclusion
carried to the excitation phase: friction is heard THROUGH the resonating
body — the scrape trains are excitation material (scrape_then_thump remains
a standout), not standalone sounds. Same lesson as breath-plus-tone: the
missing organ is coupling, not the source.

### kick (12 cells)

Verdict: **"all kicks similar issue to thump.. rumbly, no snap.. leave
that."** PARKED, no fix. Same over-wide lesson as twohit2 (bare pulses at
the wide end go sub-audio), plus the missing-snap half — noted in passing
that snap+body is exactly the combo construction, so if kicks ever return
it's as combo cells, not bare pulses. (Round-1's kick impression came from
sharp_30/80ms cells whose fast attack edge supplied the snap.)

### xthump — verdict, and a BUG that voids most of it

Matt: **"almost all have an audible pause before the thump; only goodies
are thump_cluster_tailoff and fatthump version of same."** Root cause found
and it is mechanical: seq() advanced time by each buffer's FULL length, and
place() pads 0.3 s of trailing silence — so every cluster/scrape-FIRST cell
carried ~300 ms of dead air before its thump. The two goodies are exactly
the two thump-FIRST cells (bare atoms, no embedded tail) — the only ones
heard at intended timing. seq() fixed (advances by active length);
regenerated in place: all 12 xthump originals + the 4 cluster->thump
follow-ups + the 5 texture drop_settle cells. **The cluster->thump vs
scrape->thump comparison is UNHEARD — re-listen to xthump/ before any
verdict stands.** (Round-1 scrape_then_thump was built with explicit
placement and was never affected.)

### xthump — final verdict (post seq-fix re-listen)

**"pause gone, scrapes no good, cluster>thumps are excitation candidates."**
The round-1 wondering is answered: **cluster->thump beats scrape->thump.**
Scrape is now 0-for-everything including as a lead-in — fully closed until
the excitation phase. Excitation-candidates list now: **cluster->thump
cells (incl. thump_cluster_tailoff + fatthump version), bounce2 family,
both_shrink_then_grow, dots2 family** — scrape trains kept only as raw
material for resonator coupling.
Still unverdicted in round 2: texture (crunch trajectories, holdsnap,
regenerated drop_settle).

### texture (15 cells) — final round-2 verdict

**"nothing promising in /texture .. we'll move on"** — crunch trajectories,
holdsnap variants and the (pause-fixed) drop_settle family all retire.

### NOTE for the excitation phase (Matt, 2026-08-23)

**"a fast reverse bounce resembles a 'creak' — might contribute to a string
attack."** Filed with the excitation candidates: bounce2 reverse_*_faster /
_fastest as string-attack material (creak-into-tone).

---

## ROUND 2 COMPLETE. Net state:
- **Paid off**: jagged (as a family; snare application parked with
  diagnosis), reverse bounces (creak note above), cluster->thump,
  thump_cluster_tailoff, dots2 (percussion + excitation).
- **Queued for round 3**: combo family (wide body + spike superimposed,
  bodies capped ~40 ms), pairs-of-combos replacing twohit.
- **Excitation candidates**: cluster->thump cells, bounce2 (all speeds,
  reverse=creak), both_shrink_then_grow, dots2 family, scrape trains
  (coupling-only), jagged legs.
- **Closed/parked**: scrape (raw), snares (with diagnosis), kick, twohit2,
  clusters (standalone), texture, rulebreak.

---

# Round 3 (2026-08-23): combo + combopair — 20 cells

renders/dsp/pending/segment_sweep3/. **combo** (12): body width ladder
8/15/25/40 ms (capped per the twohit2 lesson) x tri/sine, spike at onset /
20% / 40% in, spike-vs-body level ratios, fat spikes. **combopair** (8):
heel-toe at two gaps, ka-thump / thump-ka (small spike-heavy combo vs big
sine-bodied combo), equal + fat pairs, one triple_run. All amplitudes kept
hot (0.6-1.0). Awaiting verdict; excitation phase follows regardless.

---

# Excitation phase, round 1 (2026-08-23): pitched string — 13 candidates

renders/dsp/pending/excite1/ — each candidate as SegmentSource (oneShot,
normalized 0.9) into a KSPianoString (afp31_gt damper + t60 keytrack;
detune 0.5, brightness 0.926), notes 36/60/84 at 3.5 s. Auto-leveled below
the 0.7 instrument limiter (first pass pinned all 13 at exactly 0.700 —
caught and re-leveled to 0.30-0.59 peaks).
Candidates: thump_cluster_tailoff, fatthump_cluster_tailoff,
cluster_dense_thump, both_shrink_then_grow, reverse_full_faster + _fastest
(the creak → string-attack note), med_twocomp, dots_grow_boost,
dots_halfhump_6k, jagged_in_white, jagged_out_coarse, scrape_buzz (the
coupling test), combo_ref (plain body+spike as the reference strike).
Question for the verdicts: whose character SURVIVES or transforms
interestingly through the string — not who imitates piano.

### Excitation round 1 — verdict (Matt, 2026-08-23)

**"Very promising. Lots of novelty and variety, hints (in tailoffs) of
continuous excitation possibilities and... dots_halfhump_6k is at least as
good a piano as piano_default ... think the sound is better lacking the
hammer noise, which I only notice in comparison."**
Notes: (1) the tailoff cells point at the sustained/gated round;
(2) **dots_halfhump_6k = a 154-value drawn one-shot matching the four-path
noise+filter excitation of the flagship piano** — and the harness string
was afp31-flavored (brightness .926, no dispersion/inharm), not
piano_default's, so there is headroom. Built same day: piano_halfhump A/B —
piano_default's exact graph with only the excitation chain's output swapped
for the halfhump SegmentSource, rendered beside stock piano_default on the
same score (renders/dsp/pending/excite1/piano_ab/).

Interim (Matt, pre-headphones, 2026-08-23): **"guaranteed octave 1 of humpy
sounds better than default."** Full set + A/B verdict when he returns.

---

# Excitation phase, round 2 prep (2026-08-23): SUSTAINED excitation — 9 cells

Built at Matt's go, ahead of the full r1 verdicts. renders/dsp/pending/
excite2_sustain/. Mechanism: SegmentSource LOOPS (oneShot=false) with
widthVarPct/valVarPct 0.15 so each pass re-randomizes (no loop pitch), and
a bow-pressure Envelope drives seg.amplitude (sine rise 8% -> hold ->
sine settle 18%, fractions of the note). Same string harness as excite1.
Beds: scrape_buzz, scrape_fine, cluster_tailoff, shrinkgrow, jagged_out,
dots_coarse_wander (continuity 0.9 slow walk), crunch. Plus the composed
string-attack cells: **creak_into_buzz_bed / creak_into_cluster_bed** —
one-shot fast reverse bounce summed with a gated bed (CombinedSource sum).
Envelope minValue/maxValue verified by Matt this same day ("works as
advertised") — available for bed floors when tuning starts.

### Sustained probe (REVIEW 47) — verdict (Matt, 2026-08-23)

**"A lot of signs of life"** — but: **"for most at all frequencies, and for
all at high frequencies, the segment absolutely produces a 3.3 Hz (or
whatever) rhythm — a rapidly-picked zither or mandolin effect vs a
continuous bow."** Attacks are the win: "some sound really good in the
attack — noisy, chaotic, creaky/scrapy" (scrape among the leaders). His
framing: **"The grail we've been seeking is just the attack"** — the
sustained string sound is already good; tacked-on attack + crossfade onto
additive viola would work but is costly; **all-KS is elegant, tempting.**
His proposal = attack segment + sustain segment (+ maybe release segment).
Diagnosis agreed on: the zither is CONTOUR INSIDE THE LOOP — every bed was
a repurposed one-shot, so each pass restarts from zero through an onset =
a pluck per pass (worst at high f where string memory is short). Fix =
STATIONARY sustain beds (uniform statistics, 1-1.5 s, seam near zero) +
the triptych. Clarified for Matt: creak_into_* cells are TWO SegmentSources
summed by a CombinedSource (lead oneShot + looping bed), not one grafted
segment — the "recurring" sound was the bed seam, not the creak.
→ **excite3: 3 attacks x 3 stationary beds crossfaded + release-segment
cells.** Deeper wall parked for IDEAS: real bow slips phase-lock to the
string (feedback a waveform cannot express).

---

# Excitation phase, round 3 (2026-08-23): the TRIPTYCH — 11 cells

renders/dsp/pending/excite3_triptych/. Attack (one-shot: creak / chaos
scrape / jagged-in, faded by a 0.15 s-hold + 0.25 s-sine-fall seconds
envelope) + STATIONARY sustain bed (uniform statistics, ~1.2 s/pass, seam
at zero, varPct 0.12: buzz 90/s, fine 400/s, velvet 150/s) crossfaded via
the bow envelope; two cells add a release seg (fine fizzle) gated by an
envelope silent until 78% of the note. 3x3 grid + creak__buzz__rel +
jaggedin__fine__rel.
**BUG found building it, backlog-31 class in my own generated patches:**
CombinedSource's pin is `source1`, not `source` — the generic wiring
silently dropped my key, so excite3's attacks (and the excite2
creak_into_* leads!) never reached the string. Matt's "I didn't think it
was creak-like" was literally accurate: there was no creak. Fixed in both
generators; attacks verified distinct by head-RMS; the two excite2 cells
regenerated with their creaks real for the first time.

### Triptych (REVIEW 48) — verdict (Matt, 2026-08-23)

**"Pretty cool. I can still hear the segment period.. much less, and in
some almost not at all. Also the bed phase is too noisy in every case, how
can we smooth that out.. just use a sawtooth? ;-)"**
The wink = the physics: bowed steady state IS the sawtooth (Helmholtz
motion, slips locked to the string period); our beds slip at a fixed rate
regardless of pitch — aperiodic input poured over the ring.

# Excitation round 4 (2026-08-23): smoothing the bed — 9 cells

renders/dsp/pending/excite4_smooth/. Three stacked mechanisms:
A. **Duck** — bow env sustains at 0.3-0.5 instead of 1.0 (string ring
   carries the tone): creak__buzz_sus{050,030}, jaggedin__fine_sus030.
B. **Smooth bed** — 4 ms soft slips at smoothness 1.0, isolated
   (sus100) and combined with ducking (sus040): creak__smooth_*,
   jaggedin__smooth_sus040.
C. **Helmholtz layer** — drawn ramp+jag into WavetableSource.inputSource:
   the fill truncates it to ONE PERIOD AT THE NOTE FREQUENCY (truncated
   ramp = single-period saw), read pitch-locked, bow env on amplitude;
   wt.frequency added to the paramMap. creak__wtsaw_jag{15,40} +
   creak__wtsaw15_bedlow (saw + buzz bed at 0.25). Auto-level note: the
   wtsaw cells needed ~6x less volume — periodic input at f0 drives the
   combs far more efficiently, which is itself evidence the pitch-lock
   is working.

---

# THE BOW (Matt, 2026-08-23 evening)

While mapping a context-menu reorg, Matt tried **BowedStringEvolution**
(a wave-evolution node queued for retirement in backlog 25) with a
**RedNoiseSource on the bow pin at UI descriptor defaults** — frequency
440, density 0.5, smoothness 0.5, the values from the documented
descriptor-vs-constructor mismatch that nobody ever chose on purpose:
**"Sounds like a pretty fucking nice bowed string."** Then played bow
position + settings: "mind blown."
Why it works where every parallel-sum failed: the evolution modifies the
wavetable IN THE LOOP — coupled excitation, exactly what the breath+tone
post-mortem demanded; and the accidental RedNoise is a stochastic bow
(~events near the string period, density dropouts = stick-slip).
CONSEQUENCES: backlog 25's BowedString does NOT retire — it gets a family;
Matt to SAVE the exact canvas (accidental settings must not be lost);
ReedEvolution tried the same evening: "does nothing, as expected."

### excite4 — verdict (same evening)

"Excite4 was profitable too." Cells still promising; residual segment
periodicity "in some, not all."
- **wtsaw**: "basically no noisy attack" — first reaction negative, then
  RECALIBRATED against Iowa strings: "there's really no scrape or creak
  going on there at all" (arco attack is clean; the grunge was taste).
  Measured (attack-muted A/B): the creak IS present (2.6x head energy)
  but buried — auto-level scaled everything down 6x for the efficient
  wt drive; attack needs separate leveling. → creak_hot__wtsaw* cells
  rendered (attack x2.5).
- **jaggedin attacks "sound like plucks"** — Matt's suspects: jag on the
  pulse decline, or pulse too short. Mechanism agreed: the 0.15s-hold/
  0.25s-fade attack env completes DURING the 0.3 s jag rise → rise x fall
  = hump with abrupt stop = a pluck by construction. → laddered:
  jin{150,300,600,1000}_matched__fine (env fade matched to attack length)
  + jin300_oldenv__fine control. All in excite4_smooth/.
- The Listen-here question was motivated by suspecting the wtsaw attack
  was silent — answered by measurement instead (backlog 32 still blocks
  the by-ear check).

### The bow — Matt's parameter notes (2026-08-23, verbatim intent)

Patch saved: patches/pending/bow_evolution.json (Matt's sandbox); exact
bytes ALSO committed as patches/baselines/bow_evolution_discovery.json so
the accidental values have git history. Notes:
- **density (RedNoise) down → "hesitant, tentative character"**
- **frequency (RedNoise) → brightness** (as does **bow position**)
- **smoothness, continuity, rampVariation "directly translate"** — do what
  you would expect
- **boost and zeroCrossTendency TBD, "but all have an impact"**
- Squeaky/creaky attack STILL desirable for the hoe-down register — the
  grunge attacks stay in the toolkit; arco-clean and fiddle-dirty are both
  targets.
- Matt: "I owe you an apology for whatever conclusion I came to back in
  <pick a month>" — the standing lesson is nobody ever fed these nodes the
  right input: **revisit ReedEvolution and BrassEvolution** the same way
  (proper excitation on the right pin, incl. the accidental-defaults trick)
  "in case there's gold in there, too."
