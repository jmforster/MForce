"""BandedWG percussion round 1 (campaign 70, Matt's REVIEW-59 direction:
"percussive sounds - tuned drums, xylo, chimes, etc. (low-hanging fruit)").

Reuses gen_stk_bandedwg's validated struck-mode graph builder with new
mode sets (Fletcher & Rossing published ratios — steal-first), per-mode
decay gains (wood vs metal vs membrane) and strike-brightness tilts.
Rendered at 48 kHz (engine-native): these are OUR cells, not STK ports —
no reference WAV exists, so the gates are objective measurements instead
of ref-nulls (mode-1 lock, T60 sanity, the REVIEW-59 click detector).
F_CLAMP raised 1568 -> 3200 (STK's 22050 limit; 48k has the headroom and
the campaign sanctions exploration past STK's ranges) so C7 keeps its
fundamental.

Requires the adaptive ring-out engine (backlog 63b) — struck bars ring
to completion instead of clicking at the old fixed tail.

Outputs: patches/audition/bwg_perc1/ + renders/dsp/audition/bwg_perc1/.
Usage: python tools/gen_bwg_perc1.py [--only cell]
"""
import json
import math
import os
import subprocess
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_stk_bandedwg as g

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
CLI = os.path.join(ROOT, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
PATCH_OUT = os.path.join(ROOT, "patches", "audition", "bwg_perc1")
REND_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "bwg_perc1")

SR = 48000

# (modes, per-mode loop gains, excitation weights) — ratios from
# Fletcher & Rossing, Physics of Musical Instruments (2nd ed.):
# marimba/xylophone deep-arch bar tunings, vibraphone 1:4, free-bar
# chime modes, ideal circular-membrane ratios. Gains are per-loop
# feedback (T60 = period * ln(1e-3)/ln(gain), so decay shortens with
# pitch, as real bars do).
CELLS = {
    "marimba":    ([1.0, 3.9, 9.2, 20.5],
                   [0.985, 0.970, 0.950, 0.930],
                   [1.0, 0.8, 0.6, 0.4]),
    "xylophone":  ([1.0, 3.0, 5.8, 10.3],
                   [0.980, 0.965, 0.950, 0.930],
                   [1.0, 0.9, 0.7, 0.5]),
    "vibraphone": ([1.0, 3.984, 10.668],
                   [0.9975, 0.9960, 0.9940],
                   [1.0, 0.6, 0.3]),
    "chime":      ([1.0, 2.756, 5.404, 8.933, 13.345, 18.646],
                   [0.9980, 0.9980, 0.9975, 0.9970, 0.9960, 0.9950],
                   [0.5, 0.8, 1.0, 1.0, 0.8, 0.6]),
    "tomdrum":    ([1.0, 1.594, 2.136, 2.296, 2.653, 2.918],
                   [0.995, 0.992, 0.990, 0.988, 0.985, 0.980],
                   [1.0, 0.7, 0.5, 0.4, 0.3, 0.2]),
    "woodblock":  ([1.0, 2.55, 5.9],
                   [0.940, 0.910, 0.880],
                   [1.0, 0.9, 0.8]),
    "tbar_bright": (g.PRESETS["tbar"][0],
                    g.PRESETS["tbar"][1],
                    [0.4, 1.0, 1.0, 0.9]),
    "glass_long": (g.PRESETS["glass"][0],
                   [0.9985] * 5,
                   [1.0, 0.7, 0.5, 0.35, 0.25]),
}


def retune_module():
    """Re-derive gen_stk_bandedwg's rate-baked globals for 48 kHz."""
    g.SR = SR
    g.RADIUS = 1.0 - math.pi * 32.0 / SR
    g.KNORM = 0.5 * (1.0 - g.RADIUS * g.RADIUS)
    g.F_CLAMP = 3200.0


def click_scan(x, sr):
    """REVIEW-59 defect gate: loud 5 ms window followed by near-silence."""
    win = int(0.005 * sr)
    nw = len(x) // win
    if nw < 2:
        return []
    rms = np.sqrt((x[:nw * win].reshape(nw, win) ** 2).mean(axis=1))
    return [(k + 1) * win / sr for k in range(nw - 1)
            if rms[k] > 0.02 and rms[k + 1] < rms[k] * 0.05]


def mode1_cents(x, sr, t0, want):
    """Dominant spectral peak near mode 1 in the first 0.5 s of a slot."""
    a = int(t0 * sr)
    seg = x[a:a + int(0.5 * sr)]
    seg = seg * np.hanning(len(seg))
    sp = np.abs(np.fft.rfft(seg))
    freqs = np.fft.rfftfreq(len(seg), 1.0 / sr)
    band = (freqs > want * 0.90) & (freqs < want * 1.10)
    if not band.any() or sp[band].max() <= 0:
        return float("nan")
    got = freqs[band][int(np.argmax(sp[band]))]
    return 1200.0 * math.log2(got / want)


def t60_estimate(x, sr, t0, slot):
    """Decay from the slot's own envelope: -60 dB extrapolation."""
    a = int(t0 * sr)
    seg = np.abs(x[a:a + int(slot * sr)])
    win = int(0.01 * sr)
    nw = len(seg) // win
    env = seg[:nw * win].reshape(nw, win).max(axis=1)
    pk = env.max()
    if pk <= 0:
        return float("inf")
    db = 20 * np.log10(np.maximum(env / pk, 1e-6))
    # fit the -5..-35 dB stretch
    idx = np.where((db < -5) & (db > -35))[0]
    if len(idx) < 4:
        return float("inf")
    t = idx * win / sr
    slope, _ = np.polyfit(t, db[idx], 1)
    return float("inf") if slope >= -1e-3 else -60.0 / slope


def main():
    only = sys.argv[sys.argv.index("--only") + 1] if "--only" in sys.argv \
        else None
    retune_module()
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(REND_OUT, exist_ok=True)
    results = []
    for name, preset in CELLS.items():
        if only and name != only:
            continue
        g.PRESETS[name] = preset
        patch, slot = g.make_patch(name, bowed=False)
        # ring headroom past the last slot (adaptive ring-out completes it)
        patch["seconds"] = slot * len(g.NOTES) + 9.0
        ppath = os.path.join(PATCH_OUT, f"bwg_{name}.json")
        wpath = os.path.join(REND_OUT, f"bwg_{name}.wav")
        with open(ppath, "w") as f:
            json.dump(patch, f, indent=1)
        r = subprocess.run([CLI, ppath, wpath], capture_output=True,
                           text=True)
        if r.returncode != 0 or not os.path.exists(wpath):
            results.append((name, "RENDER_FAIL", r.returncode))
            continue
        x, sr = g.read_mono(wpath)
        clicks = click_scan(x, sr)
        cents = [mode1_cents(x, sr, slot * k, t)
                 for k, t in enumerate(g.TARGETS)]
        t60s = [t60_estimate(x, sr, slot * k, slot)
                for k in range(len(g.NOTES))]
        worst_c = max((abs(c) for c in cents if not math.isnan(c)),
                      default=float("nan"))
        gate = "PASS"
        why = []
        if clicks:
            gate, why = "REJECT", why + [f"clicks at {clicks}"]
        if math.isnan(worst_c) or worst_c > 40.0:
            gate, why = "REJECT", why + [f"mode1 off {worst_c:.0f}c"]
        if all(t == float("inf") for t in t60s):
            gate, why = "REJECT", why + ["no decay measured"]
        results.append((name, gate, worst_c, t60s[1], "; ".join(why)))
        print(f"{name:12s} {gate}  mode1 worst {worst_c:6.1f}c  "
              f"T60@C4 {t60s[1]:6.2f}s  {'; '.join(why)}")
    n_pass = sum(1 for r in results if r[1] == "PASS")
    print(f"\n{n_pass}/{len(results)} cells pass gates")
    # machine self-verdict: rejects don't reach the queue
    for r in results:
        if r[1] in ("REJECT", "RENDER_FAIL"):
            name = r[0]
            for path in (os.path.join(PATCH_OUT, f"bwg_{name}.json"),
                         os.path.join(REND_OUT, f"bwg_{name}.wav")):
                if os.path.exists(path):
                    os.remove(path)
            print(f"culled: {name}")


if __name__ == "__main__":
    main()
