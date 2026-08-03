"""Expand-sweep round 4 — DEEP recursion (dsp BACKLOG item 5 REOPEN, G2).

Rounds 1-3 capped recursion at 2. Item 5 parked depth explicitly: "recurse 2-4
= base*(count*2+1)^(3..5) = thousands of partials, render-bound until item 8
lands. REOPEN at recurse 2-4 after additive perf." Item 8 landed 1.2-1.6x
(run 12) then a further 1.6x/1.7x (run 14), so depth is affordable now and this
round takes it to recurse 3 and 4.

What depth actually does, which is the reason this is worth a batch:
apply_expand_rule() runs recurse+1 times, and spacing1/spacing2 are interpolated
over TIME (by multEnv/roEnv), not per level — so every level splats the SAME
cluster shape around every partial it finds. Composing that r+1 times does not
give r+1 independent structures; the offsets are sums of (2c+1)-way choices of
the same step, so the result is a comb of spacing s whose amplitude envelope is
the (r+1)-fold convolution of the level taper with itself. Two consequences
follow, and they are what the batch is designed to hear:

  1. Central limit. The cluster envelope tends to a Gaussian as depth grows,
     and its width grows like sqrt(r) in the detune term but LINEARLY in the
     spacing term (max offset = (r+1)*count*spacing). So depth trades a spiky
     comb for a smooth band, at fixed spacing.
  2. Amplitude compounding. loPct/power apply at every level, so the taper is
     raised to the (r+1)th power — deep clusters are far more peaked than the
     same total partial count laid out flat. `flat_deep` (loPct 0.9) vs
     `peaked_deep` (loPct 0.15, power 3) isolates exactly this.

So the controls matter more than usual here: `ctrl_r0_wide` lays down a
single-level cluster whose total spread matches a deep one, and `ctrl_r1_match`
matches partial COUNT at shallow depth. If depth is only "more partials", those
controls will sound like their deep counterparts and the front closes.

Sizing: partials = base * (2*count+1)^(recurse+1), so base is pulled DOWN as
depth goes up to keep each render tractable (measured, see the report). count
is mostly 1-2 — at count 3+ depth 3 the count is five figures.

Rule-breakers per WORKFLOW (a few crazy ones per batch), marked "*":
irrational spacing at depth (the comb never repeats), spacing wide enough that
most of the cloud lands above Nyquist and only the aliased/cutoff survivors
remain, per-level phase offset at depth (po compounds into a phase scramble),
and one deliberately absurd 6250-partial cell.

patches/expand_sweep4/, render + novelty-rank via _run_expand_sweep4.py.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_expand_sweep2 import make_patch  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "expand_sweep4")
os.makedirs(OUT, exist_ok=True)

# (name, base_partials, er_overrides, driver) — same shape as rounds 2/3.
# driver: None | ("adsr", decay) | ("ramp",) | ("lfo", hz)
REGIMES = [
    # --- depth ladder at fixed micro spacing: r2 -> r3 -> r4 -------------
    # The central-limit claim above is audible here or nowhere: same spacing,
    # same count, deeper. 8*3^3=216, 8*3^4=648, 4*3^5=972.
    ("micro_r2",        8, dict(count=1, recurse=2, spacing1=0.25, spacing2=0.25), None),
    ("micro_r3",        8, dict(count=1, recurse=3, spacing1=0.25, spacing2=0.25), None),
    ("micro_r4",        4, dict(count=1, recurse=4, spacing1=0.25, spacing2=0.25), None),

    # --- controls that must NOT sound like the deep cells -----------------
    # Both hold micro_r3's TOTAL SPREAD fixed at (levels * count * spacing) =
    # 4 * 1 * 0.25 = 1.0 mult, and vary only what depth is supposed to be
    # contributing:
    #   micro_r3       648 partials, 4 levels, spread 1.0   (the deep cell)
    #   ctrl_r1_match  648 partials, 2 levels, spread 1.0   (same count, half depth)
    #   ctrl_r0_wide    72 partials, 1 level,  spread 1.0   (same width, flat)
    # If micro_r3 is indistinguishable from ctrl_r1_match, depth is buying
    # nothing over count and the front closes on a measurement, not a guess.
    ("ctrl_r0_wide",    8, dict(count=4, recurse=0, spacing1=0.25, spacing2=0.25), None),
    ("ctrl_r1_match",   8, dict(count=4, recurse=1, spacing1=0.125, spacing2=0.125), None),

    # --- amplitude compounding: same geometry, opposite taper ------------
    # loPct is the live knob at count=1 (see the power trap below): every side
    # partial sits at ampl*loPct, so depth compounds it to loPct^(recurse+1).
    ("flat_deep",       8, dict(count=1, recurse=3, spacing1=0.25, spacing2=0.25,
                                loPct1=0.9, loPct2=0.9, power1=1.0, power2=1.0), None),
    ("peaked_deep",     8, dict(count=1, recurse=3, spacing1=0.25, spacing2=0.25,
                                loPct1=0.15, loPct2=0.15, power1=3.0, power2=3.0), None),

    # POWER TRAP, found by this batch and worth keeping as a regression pair.
    # apply_expand_rule tapers side partials by pow(t, power) with t = j/count
    # (left) and (count-1-j)/count (right). At count=1 both give t=0, and
    # pow(0, p)=0 for every p>0 -- so power1/power2 are INERT at count=1 and the
    # side partial always lands on the loPct floor. peaked_deep above therefore
    # rendered BYTE-IDENTICAL to micro_r3 (same sha256), which is how this
    # surfaced. These two differ ONLY in power, at count=2 where t does vary,
    # so they must differ; if they ever go identical the taper has died.
    # 2*5^4 = 1250 partials each.
    ("taper_flat_c2",   2, dict(count=2, recurse=3, spacing1=0.25, spacing2=0.25,
                                loPct1=0.15, loPct2=0.15, power1=1.0, power2=1.0), None),
    ("taper_steep_c2",  2, dict(count=2, recurse=3, spacing1=0.25, spacing2=0.25,
                                loPct1=0.15, loPct2=0.15, power1=4.0, power2=4.0), None),

    # --- detune compounding: random walk widens like sqrt(depth) ---------
    ("walk_deep",       8, dict(count=1, recurse=3, spacing1=0.25, spacing2=0.25,
                                dt1=0.12, dt2=0.12), None),

    # --- depth at musical spacings ---------------------------------------
    # semitone at r3 = a 4-semitone-wide binomial cluster per partial;
    # fifth_r3 is round-2's chordy winner ("pleasing bass whoosh") taken deeper.
    ("semitone_r3",     6, dict(count=1, recurse=3, spacing1=1.0, spacing2=1.0), None),
    ("fifth_r3",        4, dict(count=1, recurse=3, spacing1=7.0, spacing2=7.0), None),

    # --- depth + time: the comb widens DURING the note --------------------
    # spacing1 -> spacing2 under a full-length ramp, at depth: the Gaussian
    # cluster inflates continuously instead of stepping.
    ("inflate_r3",      8, dict(count=1, recurse=3, spacing1=0.1, spacing2=1.2),
                            ("ramp",)),
    # ...and a fast one, since round 3 showed fast beats slow for attacks.
    ("inflate_fast_r3", 8, dict(count=1, recurse=3, spacing1=0.1, spacing2=1.2),
                            ("adsr", 0.0125)),

    # --- rule-breakers ----------------------------------------------------
    # Irrational spacing at depth: sums of k*pi never land on a repeat, so the
    # cluster is aperiodic all the way down.
    ("*pi_r4",          4, dict(count=1, recurse=4, spacing1=3.14159,
                                spacing2=3.14159), None),
    # Most of this cloud is above Nyquist by construction (max offset =
    # 5*1*40 = 200 mult on a 220 Hz base = 44 kHz); what survives the cutoff is
    # the whole point.
    ("*supersonic_r4",  4, dict(count=1, recurse=4, spacing1=40.0, spacing2=40.0), None),
    # Phase offset compounds per level -> the partials of one cluster start
    # scattered rather than in phase; at depth this should attack differently.
    ("*phase_scram_r4", 4, dict(count=1, recurse=4, spacing1=0.3, spacing2=0.3,
                                po1=0.9, po2=0.9), None),
    # Deliberately absurd: 2*5^5 = 6250 partials.
    ("*absurd_6250",    2, dict(count=2, recurse=4, spacing1=0.2, spacing2=0.2), None),
]


def main():
    for name, base, over, driver in REGIMES:
        fname = name.lstrip("*") + ".json"
        patch = make_patch(base, over, driver)
        with open(os.path.join(OUT, fname), "w") as f:
            json.dump(patch, f, indent=1)
        est = base * (2 * over["count"] + 1) ** (over["recurse"] + 1)
        print("%-18s base=%-3d count=%d recurse=%d -> ~%d partials"
              % (name, base, over["count"], over["recurse"], est))
    print("wrote %d round-4 patches -> %s" % (len(REGIMES), OUT))


if __name__ == "__main__":
    main()
