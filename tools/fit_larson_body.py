"""Fit Larson's unpublished cello-body filter as a biquad stack (round 3).

The body filter is recovered as the LTAS ratio of his published
with/without-body WAV pairs (renders/scratch/stk_ref/mohonk05/) — four
independent pairs (b, t, bt, btd), median-combined so the ratio is the
FILTER, not the takes (STK_PORT_NOTES "LARSON BODY FILTER" section; the
coarse 09-13 numbers came from 3 pairs, this recomputes fine-grained).

Fit: parametric stack — low shelf + K peaking EQs + high shelf (RBJ
forms) — least-squares in dB over a log-frequency grid at 48 kHz (the
chassis rate). Peaks initialized at the measured curve's extrema.

Outputs:
  docs/research/stk_port/larson_body_fit.json   sections (b0,b1,b2,a1,a2)
                                                + measured curve + errors
  stdout report                                 per-section table + error

Usage: python tools/fit_larson_body.py [K_peaks]  (default 12)
"""
import json
import os
import sys
import wave

import numpy as np
from scipy import signal
from scipy.optimize import least_squares

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
REF = os.path.join(ROOT, "renders", "scratch", "stk_ref", "mohonk05")
OUT = os.path.join(ROOT, "docs", "research", "stk_port",
                   "larson_body_fit.json")
SR_FIT = 48000.0
PAIRS = [("bowedb", "bowedbbody"), ("bowedt", "bowedtbody"),
         ("bowedbt", "bowedbtbody"), ("bowedbtd", "bowedbtbodyd")]
FLO, FHI = 70.0, 9000.0        # band where the dry takes carry energy


def read_mono(name):
    w = wave.open(os.path.join(REF, name + ".wav"))
    sr, ch, n = w.getframerate(), w.getnchannels(), w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
    w.close()
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x / 32768.0, sr


def ltas_ratio_db():
    """Median with/without ratio (dB) on the welch grid, masked to bins
    where every dry take is well above its own noise floor."""
    ratios, valid = [], None
    freqs = None
    for dry_n, wet_n in PAIRS:
        dry, sr = read_mono(dry_n)
        wet, sr2 = read_mono(wet_n)
        assert sr == sr2
        nseg = 8192
        f, pd = signal.welch(dry, sr, nperseg=nseg)
        _, pw = signal.welch(wet, sr, nperseg=nseg)
        floor = np.percentile(pd[(f > FLO) & (f < sr / 2 - 500)], 10)
        ok = pd > floor * 10.0            # 10 dB over the quiet decile
        ratios.append(10.0 * np.log10(np.maximum(pw, 1e-30) /
                                      np.maximum(pd, 1e-30)))
        valid = ok if valid is None else (valid & ok)
        freqs = f
    r = np.median(np.stack(ratios), axis=0)
    spread = np.median(np.abs(np.stack(ratios) - r), axis=0)
    band = (freqs >= FLO) & (freqs <= FHI) & valid
    return freqs[band], r[band], spread[band]


def smooth_logf(f, y, frac_oct=1.0 / 6.0):
    lf = np.log2(f)
    out = np.empty_like(y)
    for i, c in enumerate(lf):
        w = np.exp(-0.5 * ((lf - c) / (frac_oct / 2.355)) ** 2)
        out[i] = np.sum(w * y) / np.sum(w)
    return out


# ----------------------------------------------------------- RBJ sections --
def peaking(fc, gain_db, q, sr=SR_FIT):
    a = 10.0 ** (gain_db / 40.0)
    w0 = 2 * np.pi * fc / sr
    al = np.sin(w0) / (2 * q)
    b = np.array([1 + al * a, -2 * np.cos(w0), 1 - al * a])
    aa = np.array([1 + al / a, -2 * np.cos(w0), 1 - al / a])
    return b / aa[0], aa / aa[0]


def shelf(fc, gain_db, low, s=1.0, sr=SR_FIT):
    a = 10.0 ** (gain_db / 40.0)
    w0 = 2 * np.pi * fc / sr
    cw, sw = np.cos(w0), np.sin(w0)
    al = sw / 2 * np.sqrt((a + 1 / a) * (1 / s - 1) + 2)
    t = 2 * np.sqrt(a) * al
    if low:
        b = a * np.array([(a + 1) - (a - 1) * cw + t,
                          2 * ((a - 1) - (a + 1) * cw),
                          (a + 1) - (a - 1) * cw - t])
        aa = np.array([(a + 1) + (a - 1) * cw + t,
                       -2 * ((a - 1) + (a + 1) * cw),
                       (a + 1) + (a - 1) * cw - t])
    else:
        b = a * np.array([(a + 1) + (a - 1) * cw + t,
                          -2 * ((a - 1) + (a + 1) * cw),
                          (a + 1) + (a - 1) * cw - t])
        aa = np.array([(a + 1) - (a - 1) * cw + t,
                       2 * ((a - 1) - (a + 1) * cw),
                       (a + 1) - (a - 1) * cw - t])
    return b / aa[0], aa / aa[0]


def stack_response(params, k, fgrid):
    """params = [gain_lo, gain_hi, (fc,g,q)*k, gain0]; shelves fixed-corner."""
    w = 2 * np.pi * fgrid / SR_FIT
    z = np.exp(-1j * w)
    h = np.full_like(z, 10.0 ** (params[-1] / 20.0), dtype=complex)
    secs = build_sections(params, k)
    for b, a in secs:
        h *= (b[0] + b[1] * z + b[2] * z * z) / \
             (1 + a[1] * z + a[2] * z * z)
    return 20.0 * np.log10(np.abs(h))


def build_sections(params, k):
    secs = [shelf(110.0, params[0], low=True),
            shelf(2600.0, params[1], low=False)]
    for i in range(k):
        fc, g, q = params[2 + 3 * i: 5 + 3 * i]
        secs.append(peaking(10.0 ** fc, g, q))
    return secs


def main():
    k = int(sys.argv[1]) if len(sys.argv) > 1 else 12
    f, r, spread = ltas_ratio_db()
    rs = smooth_logf(f, r)

    # log-spaced evaluation grid (uniform weight per octave)
    fg = np.geomspace(FLO, FHI, 400)
    tgt = np.interp(np.log10(fg), np.log10(f), rs)

    # init peaks at alternating extrema of the smoothed curve
    from scipy.signal import argrelextrema
    idx = np.sort(np.concatenate([
        argrelextrema(tgt, np.greater, order=8)[0],
        argrelextrema(tgt, np.less, order=8)[0]]))
    picks = idx[np.argsort(-np.abs(tgt[idx]))][:k]
    picks = np.sort(picks)
    x0 = [0.0, -20.0]
    lb, ub = [-30.0, -40.0], [30.0, 10.0]
    for i in range(k):
        if i < len(picks):
            fc, g = fg[picks[i]], np.clip(tgt[picks[i]], -25, 25)
        else:
            fc, g = np.geomspace(150, 5000, k)[i], 0.0
        x0 += [np.log10(fc), g, 2.0]
        lb += [np.log10(FLO), -30.0, 0.3]
        ub += [np.log10(FHI), 30.0, 12.0]
    x0.append(0.0); lb.append(-20.0); ub.append(20.0)

    res = least_squares(
        lambda p: stack_response(p, k, fg) - tgt, x0,
        bounds=(lb, ub), max_nfev=4000)
    fit = stack_response(res.x, k, fg)
    err = fit - tgt
    print("pairs: %d   bins: %d   sections: %d (2 shelves + %d peaks)"
          % (len(PAIRS), len(f), k + 2, k))
    print("pair spread (median abs dev): %.2f dB median" % np.median(spread))
    print("fit error: median %.2f dB   p95 %.2f dB   max %.2f dB"
          % (np.median(np.abs(err)), np.percentile(np.abs(err), 95),
             np.max(np.abs(err))))
    print("global gain %.1f dB  loshelf %.1f dB@110  hishelf %.1f dB@2600"
          % (res.x[-1], res.x[0], res.x[1]))
    for i in range(k):
        fc, g, q = res.x[2 + 3 * i: 5 + 3 * i]
        print("  peak %2d: %7.1f Hz  %+6.1f dB  Q %.2f"
              % (i, 10 ** fc, g, q))

    g0 = 10.0 ** (res.x[-1] / 20.0)
    sections = []
    for j, (b, a) in enumerate(build_sections(res.x, k)):
        if j == 0:
            b = b * g0                      # fold global gain into sec 0
        sections.append([round(float(v), 8) for v in
                         (b[0], b[1], b[2], a[1], a[2])])
    json.dump({
        "source": "LTAS ratio of %d Larson mohonk05 pairs" % len(PAIRS),
        "pairs": PAIRS, "sr": SR_FIT, "sections": sections,
        "fit_err_db": {"median": round(float(np.median(np.abs(err))), 3),
                       "p95": round(float(np.percentile(np.abs(err), 95)), 3),
                       "max": round(float(np.max(np.abs(err))), 3)},
        "measured_hz": [round(float(v), 2) for v in f],
        "measured_db": [round(float(v), 3) for v in rs],
    }, open(OUT, "w"), indent=1)
    print("wrote", os.path.relpath(OUT, ROOT))


if __name__ == "__main__":
    main()
