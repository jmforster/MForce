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
