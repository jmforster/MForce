"""Score the r4 critical-relative sweep (backlog 37/37a(c)).

Per cell: launch (peak), pitchedness (autocorr on the tail), MEASURED attack
time (first crossing of 10% to first crossing of 90% of peak RMS envelope,
25 ms hops), and a brightness proxy (fraction of spectral energy above the
8th harmonic). Writes all four into the manifest.

Surfacing (per Matt's r3 verdict, not global novelty): for every junction,
copy the (min offset, max attack) cell — the hug-the-threshold slow-bloom
corner that r3 never visited — plus TOP.md tables of measured attack and
brightness across the whole grid, so the offset/attack -> percept mapping
is learned even for cells nobody listens to.

Usage: python tools/_score_feedback_sweep_r4.py <patch_dir> <wav_dir> <pending_dir>
"""
import json
import os
import shutil
import sys
import wave

import numpy as np

SR = 48000


def wav_mono(path):
    w = wave.open(path)
    n, ch = w.getnframes(), w.getnchannels()
    raw = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
    w.close()
    return raw.reshape(-1, ch).mean(axis=1) / 32767.0


def attack_time(sig):
    """Returns (rise_10_90, onset_to_90). The first is the slope of the
    swell; the second includes the pre-bloom delay a slow gain ramp adds —
    perceptually both are 'attack', and a ramp that mostly delays the bloom
    is invisible to the first (r4 lesson, 2026-09-01)."""
    hop = int(0.025 * SR)
    env = np.array([np.sqrt(np.mean(sig[i:i + hop] ** 2))
                    for i in range(0, len(sig) - hop, hop)])
    if env.max() < 1e-4:
        return None, None
    lo = np.argmax(env > 0.1 * env.max())
    hi = np.argmax(env > 0.9 * env.max())
    return (round(max(0, (hi - lo)) * hop / SR, 3),
            round(hi * hop / SR, 3))


def pitchedness(sig):
    a, b = int(1.5 * SR), int(2.7 * SR)
    x = sig[a:b] if len(sig) > b else sig[len(sig) // 2:]
    x = x - x.mean()
    if len(x) < 4096 or np.abs(x).max() < 1e-4:
        return 0.0
    f = np.fft.rfft(x, 2 * len(x))
    ac = np.fft.irfft(f * np.conj(f))[:len(x)]
    if ac[0] <= 0:
        return 0.0
    ac /= ac[0]
    lo, hi = SR // 1200, SR // 30
    return float(ac[lo:min(hi, len(ac) - 1)].max())


def brightness(sig, f0):
    a, b = int(1.5 * SR), int(2.7 * SR)
    x = sig[a:b] if len(sig) > b else sig[len(sig) // 2:]
    if np.abs(x).max() < 1e-4:
        return 0.0
    S = np.abs(np.fft.rfft(x * np.hanning(len(x)))) ** 2
    freqs = np.fft.rfftfreq(len(x), 1 / SR)
    tot = S.sum()
    return round(float(S[freqs > 8 * f0].sum() / tot), 3) if tot > 0 else 0.0


def main():
    patch_dir, wav_dir, pending = sys.argv[1], sys.argv[2], sys.argv[3]
    man_path = os.path.join(patch_dir, "manifest.json")
    man = json.load(open(man_path))
    for v in man["variants"]:
        wp = os.path.join(wav_dir, v["file"])
        if not os.path.exists(wp):
            v["status"] = "RENDER_FAIL"
            continue
        sig = wav_mono(wp)
        peak = float(np.abs(sig).max())
        v["peak"] = round(peak, 4)
        if peak < 0.02:
            v["status"] = "DEAD"
            continue
        f0 = 440.0 * 2 ** ((v["params"]["note"] - 69) / 12)
        v["status"] = "OK"
        v["attack_measured"], v["bloom90"] = attack_time(sig)
        v["periodicity"] = round(pitchedness(sig), 3)
        v["brightness"] = brightness(sig, f0)
    json.dump(man, open(man_path, "w"), indent=1)

    os.makedirs(pending, exist_ok=True)
    by_junction = {}
    for v in man["variants"]:
        by_junction.setdefault(v["params"]["junction"], []).append(v)

    min_off, max_atk = min(man["offsets"]), max(man["attacks"])
    lines = ["# r4 critical-relative sweep — per-junction slow-bloom corner",
             f"(offsets {man['offsets']}, attacks {man['attacks']} s; copied "
             f"cell = offset {min_off}, attack {max_atk} s; full grid + "
             "metrics in the sweep manifest)", ""]
    copied = 0
    for name, cells in sorted(by_junction.items()):
        ok = [c for c in cells if c.get("status") == "OK"]
        dead = len(cells) - len(ok)
        pick = next((c for c in ok
                     if c["params"]["offset"] == min_off
                     and c["params"]["attack_s"] == max_atk), None)
        if pick is None and ok:
            pick = sorted(ok, key=lambda c: (c["params"]["offset"],
                                             -c["params"]["attack_s"]))[0]
        if pick:
            shutil.copy2(os.path.join(wav_dir, pick["file"]),
                         os.path.join(pending, pick["file"]))
            copied += 1
        # attack + brightness across the grid, offset rows x attack cols
        lines.append(f"## {name}  (critical {cells[0]['params']['critical']}"
                     + (f", {dead} dead" if dead else "") + ")")
        for metric in ("attack_measured", "bloom90", "brightness"):
            lines.append(f"{metric}:")
            for off in man["offsets"]:
                row = []
                for atk in man["attacks"]:
                    c = next((c for c in cells
                              if c["params"]["offset"] == off
                              and c["params"]["attack_s"] == atk), None)
                    val = c.get(metric) if c and c.get("status") == "OK" else None
                    row.append("dead" if val is None else f"{val}")
                lines.append(f"  off {off:+.0%}: " + "  ".join(row))
        lines.append("")
    with open(os.path.join(pending, "TOP.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    n_ok = sum(1 for v in man["variants"] if v.get("status") == "OK")
    n_dead = sum(1 for v in man["variants"] if v.get("status") == "DEAD")
    print(f"{n_ok} OK, {n_dead} dead of {len(man['variants'])}; "
          f"{copied} slow-bloom corners -> {pending}")


main()
