"""Step-response verification for SlewLimiterSource (backlog 3g).

Renders are a 5 Hz square (+-1 before the -3 dB mono pan) through the
limiter. Slew mode must produce a STRAIGHT ramp whose duration is
step_size / rate; Lag mode must produce an exponential approach with time
constant 1/rate. Measured, not eyeballed.

Usage: python tools/verify_slew.py
"""
import struct
import sys
import wave
from pathlib import Path

RENDERS = Path(__file__).resolve().parent.parent / "renders" / "slew_test"
SR = 48000


def read_mono(path):
    with wave.open(str(path), "rb") as w:
        n = w.getnframes()
        ch = w.getnchannels()
        sw = w.getsampwidth()
        raw = w.readframes(n)
    assert sw == 2, f"expected 16-bit, got {sw*8}"
    vals = struct.unpack("<%dh" % (len(raw) // 2), raw)
    if ch == 2:
        vals = vals[0::2]
    return [v / 32768.0 for v in vals]


def first_rising_edge(x, lo_frac=0.05, hi_frac=0.95):
    """Find the first low->high transition and return (t10, t90) in samples.

    Levels are taken from the signal's own extremes, so the -3 dB pan and
    any residual scaling drop out.
    """
    lo, hi = min(x), max(x)
    span = hi - lo
    lo_th = lo + lo_frac * span
    hi_th = lo + hi_frac * span
    i = 0
    n = len(x)
    # skip the priming sample and any initial high region
    while i < n and x[i] > lo_th:
        i += 1
    while i < n and x[i] <= lo_th:
        i += 1
    if i >= n:
        return None
    start = i - 1
    while i < n and x[i] < hi_th:
        i += 1
    if i >= n:
        return None
    return start, i


def linearity(x, a, b):
    """Max deviation from a straight line through (a, x[a]) and (b, x[b]),
    as a fraction of the traversed span."""
    if b - a < 2:
        return 0.0
    y0, y1 = x[a], x[b]
    slope = (y1 - y0) / (b - a)
    worst = 0.0
    for k in range(a, b + 1):
        worst = max(worst, abs(x[k] - (y0 + slope * (k - a))))
    span = abs(y1 - y0)
    return worst / span if span else 0.0


def main():
    # The limiter sees the PulseSource output directly: amplitude 1.0, so the
    # step at its INPUT is exactly 2.0 units. The rendered file is that signal
    # after the output stage's mono pan, so the rendered span is smaller. Every
    # prediction below is made in input units — no pan convention is assumed
    # (that is backlog item 3d's open question, not this test's business).
    STEP_INPUT = 2.0
    ctrl = read_mono(RENDERS / "step_ctrl.wav")
    span = max(ctrl) - min(ctrl)
    print(f"control step: {STEP_INPUT:.3f} at limiter input, "
          f"{span:.6f} rendered (output gain {span/STEP_INPUT:.6f})")

    fails = []
    cases = [
        ("step_slew_r200.wav", "slew", 200.0),
        ("step_slew_r2000.wav", "slew", 2000.0),
        ("step_slew_asym.wav", "slew", 2000.0),
        ("step_lag_r200.wav", "lag", 200.0),
    ]
    for name, mode, rate in cases:
        x = read_mono(RENDERS / name)
        edge = first_rising_edge(x)
        if edge is None:
            print(f"{name}: NO EDGE FOUND")
            fails.append(name)
            continue
        a, b = edge
        # full 0->100% traversal time implied by the measured 5%..95% window
        meas_full = (b - a) / 0.90 / SR
        if mode == "slew":
            pred = STEP_INPUT / rate
            err = abs(meas_full - pred) / pred
            lin = linearity(x, a, b)
            ok = err < 0.05 and lin < 0.02
            print(f"{name}: rise(5-95%)={(b-a)/SR*1000:.3f} ms  "
                  f"implied full={meas_full*1000:.3f} ms  predicted={pred*1000:.3f} ms  "
                  f"err={err*100:.2f}%  nonlinearity={lin*100:.3f}%  "
                  f"{'PASS' if ok else 'FAIL'}")
        else:
            # one-pole: 5%->95% takes ln(0.95/0.05) = 2.944 time constants
            tau_meas = (b - a) / SR / 2.9444
            pred_tau = 1.0 / rate
            err = abs(tau_meas - pred_tau) / pred_tau
            lin = linearity(x, a, b)
            ok = err < 0.05 and lin > 0.02  # must NOT be straight
            print(f"{name}: rise(5-95%)={(b-a)/SR*1000:.3f} ms  "
                  f"tau={tau_meas*1e3:.3f} ms  predicted tau={pred_tau*1e3:.3f} ms  "
                  f"err={err*100:.2f}%  nonlinearity={lin*100:.3f}% (expected >2%)  "
                  f"{'PASS' if ok else 'FAIL'}")
        if not ok:
            fails.append(name)

    # asymmetry: fall is rate-limited at fallRate (200), rise at rate (2000),
    # so the fall must take 10x as long. Measure from the END of the high
    # plateau — the square is high for 100 ms, and counting that in would make
    # any fall look slow.
    x = read_mono(RENDERS / "step_slew_asym.wav")
    lo, hi = min(x), max(x)
    sp = hi - lo
    hi_th, lo_th = lo + 0.95 * sp, lo + 0.05 * sp
    i = 0
    while i < len(x) and x[i] < hi_th:
        i += 1
    while i < len(x) and x[i] >= hi_th:   # walk to the end of the plateau
        i += 1
    start = i - 1
    while i < len(x) and x[i] > lo_th:
        i += 1
    fall_meas = (i - start) / 0.90 / SR    # 95..5% -> implied full traversal
    fall_pred = STEP_INPUT / 200.0
    rise_pred = STEP_INPUT / 2000.0
    err = abs(fall_meas - fall_pred) / fall_pred
    print(f"step_slew_asym implied full fall={fall_meas*1000:.3f} ms  "
          f"predicted={fall_pred*1000:.3f} ms  err={err*100:.2f}%  "
          f"(rise predicted {rise_pred*1000:.3f} ms -> "
          f"asymmetry {fall_meas/rise_pred:.2f}x, expected 10x)")
    if err < 0.05:
        print("  PASS: fallRate override honoured")
    else:
        print("  FAIL: fallRate override not honoured")
        fails.append("asym-fall")

    # --- Peak mode vs impulses -------------------------------------------
    # Velvet emits +-1 one-sample impulses at 10/s, rate is effectively
    # instant, fallRate 100 u/s => each glide should last 1.0/100 = 10 ms.
    # Peak must do that for BOTH polarities; sign-keyed Slew only does it for
    # positive-going ones, because a negative impulse's delta is negative and
    # therefore takes the slow fallRate on the way OUT.
    def glides(path):
        x = read_mono(path)
        pos = neg = 0
        run_sign = 0
        run_len = 0
        out = {"pos": [], "neg": []}
        for v in x:
            s = 1 if v > 1e-4 else (-1 if v < -1e-4 else 0)
            if s == run_sign and s != 0:
                run_len += 1
            else:
                if run_sign > 0:
                    out["pos"].append(run_len)
                elif run_sign < 0:
                    out["neg"].append(run_len)
                run_sign, run_len = s, 1 if s else 0
        if run_sign > 0:
            out["pos"].append(run_len)
        elif run_sign < 0:
            out["neg"].append(run_len)
        return out

    pred_ms = 1.0 / 100.0 * 1000.0
    for name in ("step_peak_impulse.wav", "step_slew_impulse.wav"):
        g = glides(RENDERS / name)
        # renders are stereo-interleaved-then-decimated to one channel already
        def med(a):
            a = sorted(a)
            return (a[len(a) // 2] / SR * 1000.0) if a else 0.0
        print(f"{name}: {len(g['pos'])} positive glides (median "
              f"{med(g['pos']):.2f} ms), {len(g['neg'])} negative "
              f"(median {med(g['neg']):.2f} ms), predicted {pred_ms:.2f} ms")
        if name.startswith("step_peak"):
            ok = (abs(med(g["pos"]) - pred_ms) / pred_ms < 0.10
                  and abs(med(g["neg"]) - pred_ms) / pred_ms < 0.10)
            print(f"  {'PASS' if ok else 'FAIL'}: Peak glides symmetric "
                  f"in both polarities")
            if not ok:
                fails.append(name)
        else:
            # documentary, not a gate: shows WHY Peak mode exists
            print("  (Slew mode reference — asymmetry here is the reason "
                  "Peak mode was added, not a defect)")

    print()
    if fails:
        print("FAILURES:", ", ".join(fails))
        return 1
    print("ALL PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
