"""1D Pierce probe round 2 — the real passive nonlinear filter as a KS
termination.

Probe 1 (REVIEW 61) had no Pierce filter to use: it approximated
"sign-dependent termination stiffness" by driving an SVF's cutoff from the
signal through a CurveNode. Matt liked the plucked strings that came out, so
this round keeps the 1D form and drops the actual filter in.

`PierceFilter` is the Pierce/Van Duyne passive nonlinear allpass
(JASA 101(2) 1120-1126, 1997) as a standalone graph node — the same filter
`Mesh2D` `edgeMode 2` puts on each edge node. Its coefficient is a SPRING
STIFFNESS switched by the sign of its own internal state: `coefNeg` while that
state is negative, `coefPos` while it is >= 0. It sits in the KS loop where the
bridge would be:

    Noise burst --> [+] --> DelayLine --> damp LPF --> PierceFilter --> out
                     ^                                       |
                     +------------------ tap ----------------+

Why a node and not a Shaper mode or Biquad pins: Shaper is a memoryless drawn
curve, Biquad designs its coefficients from fc/Q. This filter carries its own
state `u` and its defining behaviour is that the coefficient is picked by
sign(u) with the state carried across the change at constant energy — neither
node has anywhere to put that. See engine/include/mforce/source/pierce_filter.h.

CELLS (4, per the ears budget). Every Pierce cell uses a symmetric pair, so its
mean stiffness is 0 and the CONTROL is the same graph with
`coefNeg == coefPos == 0` — an exactly linear allpass, i.e. a plain pluck. The
asymmetry is therefore the only difference, not a side effect of a different
average termination stiffness.

  ctl     (0.000, 0.000)   linear — the control pluck
  weak    (0.300, -0.300)
  strong  (0.700, -0.700)
  max     (0.999, -0.999)  the strongest passivity-safe setting (the clamp)

Because the filter is passive, all of these run at full drive with no clamp.

Gates: feature audibility vs the control (>= 3 percentage points of high-band
fraction OR >= 3 differing top-10 peaks), level ceiling, audibility floor,
click scan, output-dir ownership purge. Probe 1's modal-upwelling ratio is
reported alongside but does not gate.

Outputs: patches/audition/pierce1d_2/ + renders/dsp/audition/pierce1d_2/.
Usage: python tools/gen_pierce_probe2.py [--only cell]
"""
import json
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_stk_bandedwg import add, mul  # noqa: E402
from gen_stk_bowed import read_mono  # noqa: E402
from gen_stk_mesh2d import peaks, top_peaks  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "audition", "pierce1d_2")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "pierce1d_2")

SR = 48000
DAMP_FC = 5000.0
NOTES = [48, 60]          # C3, C4, as in probe 1
SLOT = 5.0
SECONDS = SLOT * len(NOTES) + 2.0
BURST = 0.003             # 3 ms noise burst = the pluck
DRIVE = 0.9               # full drive; passive, so nothing needs halving

CAL_GAIN = 0.3
CAL_TARGET = 0.25
GAIN_MIN, GAIN_MAX = 0.02, 6.0

CELLS = {
    "ctl":    (0.0,   0.0),
    "weak":   (0.3,  -0.3),
    "strong": (0.7,  -0.7),
    "max":    (0.999, -0.999),
}
CONTROL = "ctl"

# Feature-audibility gate. F_LO is 80 Hz here, not the mesh's 200 — a C3 pluck
# has its fundamental at 131 Hz and the whole point is the balance between it
# and what grows above it.
F_LO, F_MID, F_HI = 80.0, 2000.0, 15000.0
HB_MIN_DELTA_PP = 3.0
PEAK_N = 10
PEAK_FLOOR_DB = -40.0
PEAK_TOL = 0.01
PEAK_MIN_DIFF = 3
LEVEL_CEILING = 0.5
AUDIBILITY_FLOOR = 1e-4
DROPOUT_PERSIST_MS = 50.0   # see dropout_scan()

# Probe 1's modal-upwelling metric, reported not gated.
UP_HI = (1500.0, 6000.0)
UP_LO = (40.0, 800.0)
EARLY = (0.1, 0.6)
LATE = (2.0, 3.0)


def make_patch(coefNeg, coefPos, gain):
    R = lambda i: {"ref": i}
    T = lambda i: {"tap": i}
    nodes = [
        {"id": "__perf_freq", "type": "PerformNode",
         "params": {"field": "frequency"}},
        {"id": "Burst_env", "type": "Envelope", "params": {
            "minValue": 0.0, "maxValue": DRIVE,
            "ramp_accuracy": 1.0, "stage_accuracy": 1.0,
            "timeMode": "seconds", "timeScale": 1.0,
            "stages": [
                {"type": "Linear", "startVal": 1.0, "endVal": 1.0,
                 "percent": BURST, "power": 0.0, "minSec": 0.0,
                 "maxSec": 0.0},
                {"type": "Linear", "startVal": 0.0, "endVal": 0.0,
                 "percent": 1.0, "power": 0.0, "minSec": 0.0,
                 "maxSec": 0.0}]}},
        {"id": "Noise", "type": "WhiteNoiseSource", "params": {
            "amplitude": R("Burst_env"), "density": 1.0, "boost": 0.0,
            "continuity": 0.0, "zeroCrossTendency": 0.0}},
        # The loop. Only ONE tap (the loop closure); everything else is a
        # real ref, so the chain advances exactly once per sample.
        add("DIn", R("Noise"), T("Pierce")),
        {"id": "Delay_line", "type": "DelayLine", "params": {
            "source": R("DIn"), "frequency": R("__perf_freq"),
            "ratio": 1.0, "amplitude": 0.998, "compensate": True}},
        {"id": "Damp_lpf", "type": "SVFSource", "params": {
            "source": R("Delay_line"), "cutoffFreq": DAMP_FC,
            "resonance": 0.7, "mode": "lowpass"}},
        # The bridge: Pierce/Van Duyne passive nonlinear allpass.
        {"id": "Pierce", "type": "PierceFilter", "params": {
            "source": R("Damp_lpf"), "coefNeg": coefNeg,
            "coefPos": coefPos}},
        mul("Out", R("Pierce"), round(gain, 6)),
    ]
    return {
        "sampleRate": SR, "seconds": SECONDS,
        "instrument": {"polyphony": 1, "volume": 0.7},
        "score": [{"note": n, "time": SLOT * k, "duration": 4.0,
                   "velocity": 0.8} for k, n in enumerate(NOTES)],
        "graph": {"output": "Out", "nodes": nodes},
        "ui": {"noteFaces": [{"fields": {"frequency": "__perf_freq"},
                              "label": "Note1"}]},
    }


def rms(x):
    return float(np.sqrt((x ** 2).mean())) if len(x) else 0.0


def level_ceiling(x, sr):
    win = int(0.5 * sr)
    nw = len(x) // win
    if nw == 0:
        return rms(x)
    w = np.sqrt((x[:nw * win].reshape(nw, win) ** 2).mean(axis=1))
    return float(w.max())


def high_band_fraction(x, sr):
    seg = x * np.hanning(len(x))
    sp = np.abs(np.fft.rfft(seg)) ** 2
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    hi = sp[(fr > F_MID) & (fr < F_HI)].sum()
    tot = sp[(fr > F_LO) & (fr < F_HI)].sum()
    return float(hi / tot) if tot > 0 else float("nan")


def up_ratio(x, sr, a_s, b_s):
    seg = x[int(a_s * sr):int(b_s * sr)]
    if len(seg) < 16:
        return float("nan")
    seg = seg * np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(seg)) ** 2
    fr = np.fft.rfftfreq(len(seg), 1.0 / sr)
    hi = sp[(fr > UP_HI[0]) & (fr < UP_HI[1])].sum()
    lo = sp[(fr > UP_LO[0]) & (fr < UP_LO[1])].sum()
    return float(hi / lo) if lo > 0 else float("nan")


def peak_set(x, sr):
    f, m = peaks(x, sr)
    if len(f) == 0:
        return []
    thr = 10.0 ** (PEAK_FLOOR_DB / 20.0)
    return [pf for pf, pm in top_peaks(f, m, PEAK_N) if pm >= thr]


def peak_mismatch(a, b):
    def miss(p, q):
        return sum(1 for f in p
                   if not any(abs(f - g) / f <= PEAK_TOL for g in q))
    return max(miss(a, b), miss(b, a))


def null_events(x, sr):
    """5 ms windows where a loud window is followed by near-silence —
    gen_bwg_perc1.click_scan's condition, evaluated here so the recovery can
    be measured too. Returns (index, recovery_ms or None) per event."""
    win = int(0.005 * sr)
    nw = len(x) // win
    if nw < 2:
        return []
    e = np.sqrt((x[:nw * win].reshape(nw, win) ** 2).mean(axis=1))
    out = []
    for k in range(nw - 1):
        if e[k] > 0.02 and e[k + 1] < e[k] * 0.05:
            rec = next(((j - k) * 5 for j in range(k + 1, min(k + 400, nw))
                        if e[j] > 0.2 * e[k]), None)
            out.append(((k + 1) * win / sr, rec))
    return out


def dropout_scan(x, sr, persist_ms=DROPOUT_PERSIST_MS):
    """The REVIEW-59 defect gate, made persistence-aware.

    `click_scan` flags any loud 5 ms window followed by near-silence. That
    catches a render that DIES, which is what it was written for, but it also
    catches a deep envelope null in a string that is still ringing — and a
    Pierce-terminated loop beats deeply, so it produces those routinely
    (measured: every event in this cell set is back above 20% of its
    pre-drop level within 10 ms, while the control pluck produces none).
    Worse, the 0.02 threshold is absolute, so the same render flags or does
    not flag purely according to the listening trim.

    A defect is a dropout that STAYS down. This returns only events that do
    not recover within `persist_ms`."""
    return [t for t, rec in null_events(x, sr)
            if rec is None or rec >= persist_ms]


def render(name, patch, quiet=False):
    ppath = os.path.join(PATCH_OUT, f"pierce1d_2_{name}.json")
    wpath = os.path.join(REND_OUT, f"pierce1d_2_{name}.wav")
    with open(ppath, "w") as f:
        json.dump(patch, f, indent=1)
    r = subprocess.run([CLI, ppath, wpath], capture_output=True, text=True,
                       timeout=1200)
    if r.returncode != 0 or not os.path.exists(wpath):
        if not quiet:
            print(f"{name:8s} RENDER_FAIL {(r.stderr or '')[-200:]}")
        return None, ppath, wpath
    x, sr = read_mono(wpath)
    return (x, sr), ppath, wpath


README = """# 1D Pierce probe round 2 — the real passive nonlinear filter as a bridge

Renders: `renders/dsp/audition/pierce1d_2/`; patches alongside in
`patches/audition/pierce1d_2/`. Generator: `tools/gen_pierce_probe2.py`.

Probe 1 had no Pierce filter to use, so it approximated "sign-dependent
termination stiffness" by driving an SVF's cutoff from the signal through a
CurveNode. Matt liked the plucked strings that came out. This round keeps the
1D form and drops the actual filter in.

`PierceFilter` is Pierce & Van Duyne's passive nonlinear allpass
(JASA **101**(2) 1120-1126, 1997) as a graph node — the same filter `Mesh2D`
`edgeMode 2` puts on every edge node, exposed separately so it can terminate a
1D loop. Its coefficient is a **spring stiffness** switched by the sign of its
own internal state: `coefNeg` while that state is negative, `coefPos` while it
is >= 0. Structure, provenance, and why the literal recurrence from the patent
had to be tightened to be genuinely passive:
`docs/research/stk_port/PIERCE_PASSIVE_NOTES.md`. Passivity gate, run against
the shipped node by `tools/engine_tests`: worst cumulative output/input energy
**{passivity}** over 48 open-loop probes, gate `<= 1+1e-6`.

The graph is a Karplus-Strong loop with the filter where the bridge would be:

    Noise burst --> [+] --> DelayLine --> damp LPF (5 kHz) --> PierceFilter --> out
                     ^                                              |
                     +--------------------- tap --------------------+

Because the filter is passive, the loop needs no clamp and runs at full drive.

## Cells

Every Pierce cell uses a **symmetric** pair, so its mean stiffness is zero and
the control — the same graph with `coefNeg == coefPos == 0`, an exactly linear
allpass — is a matched twin: the **asymmetry** is the only difference, not a
different average termination stiffness.

| cell | (coefNeg, coefPos) | what it is |
|------|--------------------|------------|
| `ctl` | (0.000, 0.000) | linear allpass — the control pluck |
| `weak` | (0.300, -0.300) | gentle switch |
| `strong` | (0.700, -0.700) | hard switch |
| `max` | (0.999, -0.999) | the strongest passivity-safe setting (the clamp) |

Two plucks per render, C3 then C4, 5 s apart, 3 ms noise burst.

**A thing to expect, not a bug.** A first-order allpass's phase delay depends
on its coefficient, so a stiffer termination shortens the effective string:
the Pierce cells will not sit at exactly the same pitch as the control, and
`max` least of all. That is the mechanism, not a tuning error — the loop is
compensated for the delay line, not for a boundary whose stiffness changes
twice a cycle.

## Metrics

{table}

`hbFrac` = fraction of in-band (80 Hz - 15 kHz) energy above 2 kHz over the
whole render; gain-invariant, so the listening trim cannot move it. `dHB` =
percentage points against the control. `dPeaks` = how many of the cell's top-10
spectral peaks (>= -40 dB) have no partner within 1% in the control, worse
direction. `up(early/late)` is probe 1's modal-upwelling ratio — 1.5-6 kHz over
40-800 Hz energy, in windows 0.1-0.6 s and 2.0-3.0 s after the C4 pluck;
`upwell` above 1 means the upper components are **gaining** on the fundamental
while the note decays, which is the gong claim. Reported, not gated. `gain` is
a listening trim only.

**Measured result on that one: no cell upwells.** Every Pierce cell puts far
more energy in the high band than the control does *early* (`early` 0.33 for
`weak` against 0.011 for the control), and every cell's `late` is at or near
zero. The high band is created at the boundary and then removed by the 5 kHz
damping filter in the loop faster than the boundary refills it. So what this
filter delivers in 1D is a bright, rich attack, not a swell — whether the swell
exists at all in this port is still open, and the mesh cells
(`renders/dsp/audition/mesh2d_ext2/`) are where to look for it.

**Audibility gate** (the round-1 lesson): a cell ships only if `|dHB| >= 3`
percentage points or `dPeaks >= 3` against the control.

`nulls` is worth a word, because it is the one thing in this set that had to
be looked at rather than gated on. The house defect gate
(`gen_bwg_perc1.click_scan`) flags any loud 5 ms window followed by
near-silence, which is right for catching a render that dies. A
Pierce-terminated loop produces those routinely *while still ringing* — the
asymmetric boundary makes a dense, beating mode set, and the beats go deep.
Measured: every flagged event in this set is back above 20% of its pre-drop
level within **10 ms**, and the control pluck produces none at all. The gate
here therefore requires the silence to persist 50 ms before it counts as a
defect, and the number of deep nulls is reported instead, because **it is
audible** — these strings warble, and `strong` warbles most. Whether that is
a string or a fault is an ear question, which is why it is a column and not a
cull.

{cull}

## THE QUESTION

Probe 1's strings were the part of the mesh thread Matt liked. So:

1. **Does the real filter beat the stand-in?** These against
   `renders/dsp/audition/pierce1d_1/` — the SVF-cutoff approximation. Same
   mechanism claim, one of them actual physics. Is the difference audible, and
   is it in the right direction?
2. **Where does the asymmetry stop helping?** `weak` to `strong` to `max` is a
   monotone axis in the filter and emphatically not one in the ear. Which one
   is a string worth keeping, and does `max` go somewhere interesting or just
   somewhere strange?
"""


def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv \
        else None
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)
    keep = {f"pierce1d_2_{n}.json" for n in CELLS} | \
           {f"pierce1d_2_{n}.wav" for n in CELLS} | {"README.md"}
    for d in (PATCH_OUT, REND_OUT):
        for fn in os.listdir(d):
            if fn not in keep:
                os.remove(os.path.join(d, fn))
                print(f"purged stale: {fn}")

    # Pass 1: one listening trim for the whole set, from the loudest cell —
    # so relative loudness between cells survives, which is part of what the
    # asymmetry does.
    loudest = 0.0
    for name, (cn, cp) in CELLS.items():
        got, _, _ = render(name, make_patch(cn, cp, CAL_GAIN), quiet=True)
        if got is None:
            print(f"{name:8s} RENDER_FAIL in calibration pass")
            return
        loudest = max(loudest, level_ceiling(got[0], got[1]))
    gain = float(np.clip(CAL_GAIN * CAL_TARGET / max(loudest, 1e-9),
                         GAIN_MIN, GAIN_MAX))

    # Pass 2
    out = {}
    for name, (cn, cp) in CELLS.items():
        got, pp, wp = render(name, make_patch(cn, cp, gain))
        if got is None:
            return
        out[name] = (got[0], got[1], pp, wp)

    xc, sr = out[CONTROL][0], out[CONTROL][1]
    hb_c, pk_c = high_band_fraction(xc, sr), peak_set(xc, sr)

    print("%-8s %8s %8s %6s %7s %6s %8s %8s %7s %7s %7s  %s"
          % ("cell", "hbFrac", "dHB(pp)", "dPeak", "modes", "nulls", "early",
             "late", "upwell", "winRms", "rms", "verdict"))
    rows, culls = [], []
    for name in CELLS:
        if only and name != only:
            continue
        x = out[name][0]
        hb = high_band_fraction(x, sr)
        pk = peak_set(x, sr)
        d_hb = (hb - hb_c) * 100.0
        d_pk = peak_mismatch(pk, pk_c)
        e = up_ratio(x, sr, SLOT + EARLY[0], SLOT + EARLY[1])
        l = up_ratio(x, sr, SLOT + LATE[0], SLOT + LATE[1])
        up = l / e if e and e == e and e > 0 else float("nan")
        wr = level_ceiling(x, sr)
        tot = rms(x)

        bad = []
        if wr > LEVEL_CEILING:
            bad.append(f"LEVEL {wr:.2f}")
        if tot < AUDIBILITY_FLOOR:
            bad.append(f"SILENT {tot:.2e}")
        nulls = len(null_events(x, sr))
        cl = dropout_scan(x, sr)
        if cl:
            bad.append(f"dropout at {cl[:3]}")
        if name != CONTROL and not bad \
                and abs(d_hb) < HB_MIN_DELTA_PP and d_pk < PEAK_MIN_DIFF:
            bad.append(f"NEAR-DUPLICATE of control (dHB {d_hb:+.1f} pp, "
                       f"dPeaks {d_pk})")

        if bad:
            reason = ", ".join(bad)
            culls.append((name, reason))
            for p in out[name][2:4]:
                if os.path.exists(p):
                    os.remove(p)
            print("%-8s %8.4f %8s %6d %7d %6d %8.3f %8.3f %7.2f %7.3f %7.4f  "
                  "CULLED: %s" % (name, hb, "%+.1f" % d_hb, d_pk, len(pk),
                                  nulls, e, l, up, wr, tot, reason),
                  flush=True)
            continue
        rows.append((name, hb, d_hb, d_pk, len(pk), nulls, e, l, up, wr, tot,
                     gain))
        print("%-8s %8.4f %8s %6s %7d %6d %8.3f %8.3f %7.2f %7.3f %7.4f  ok"
              % (name, hb, "%+.1f" % d_hb if name != CONTROL else "",
                 str(d_pk) if name != CONTROL else "", len(pk), nulls, e, l,
                 up, wr, tot), flush=True)

    if only:
        return
    tbl = ["| cell | hbFrac | dHB (pp) | dPeaks | modes | nulls | early | "
           "late | upwell | winRms | rms | gain |",
           "|------|--------|----------|--------|-------|-------|-------|"
           "------|--------|--------|-----|------|"]
    for r in rows:
        tbl.append("| `%s` | %.4f | %s | %s | %d | %d | %.3f | %.3f | "
                   "**%.2f** | %.3f | %.4f | %.3f |"
                   % (r[0], r[1],
                      "**%+.1f**" % r[2] if r[0] != CONTROL else "",
                      "**%d**" % r[3] if r[0] != CONTROL else "",
                      r[4], r[5], r[6], r[7], r[8], r[9], r[10], r[11]))
    cull = ("**Culled this round:**\n\n"
            + "\n".join("* `%s` — %s" % c for c in culls)) if culls else \
        ("**Nothing was culled**: every cell cleared the audibility gate "
         "against the control pluck.")
    with open(os.path.join(REND_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{table}", "\n".join(tbl))
                      .replace("{cull}", cull)
                      .replace("{passivity}", "1.000000019"))
    print(f"\n{len(rows)} cells shipped, {len(culls)} culled")
    print(f"README -> {os.path.join(REND_OUT, 'README.md')}")


if __name__ == "__main__":
    main()
