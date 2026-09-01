"""Score + rank a feedback curve sweep (backlog 37, select phase).

1. Build the novelty library npz from a reference-WAV dir (skipped if the
   npz already exists).
2. Embed every candidate WAV, mark DEAD anything under the peak floor
   (a curve that never bloomed), score the rest as novelty = distance to
   nearest library neighbour.
3. Write scores into the sweep manifest, copy the top K WAVs into the
   listening queue, and emit TOP.md describing each pick's drawn params.

Usage: python tools/_score_feedback_sweep.py <lib_wav_dir> <lib_npz>
           <sweep_patch_dir> <sweep_wav_dir> <pending_dir> [topK]
"""
import json, os, shutil, struct, sys, wave

import numpy as np

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
sys.path.insert(0, os.path.join(ROOT, "research", "novelty"))
import embedding as emb            # noqa: E402
import novelty as nov              # noqa: E402

PEAK_FLOOR = 0.02
PERIODIC_FLOOR = 0.4   # autocorr peak below this = chaos, ranked separately


def wav_mono(path):
    w = wave.open(path)
    n, ch = w.getnframes(), w.getnchannels()
    raw = np.frombuffer(w.readframes(n), dtype=np.int16).astype(np.float64)
    w.close()
    return raw.reshape(-1, ch).mean(axis=1) / 32767.0, w.getframerate()


def pitchedness(sig, sr):
    """Normalized autocorrelation peak over 30-1200 Hz lags, measured on the
    SUSTAINED region (0.5-1.5 s) — 1.0 = perfectly periodic, ~0 = noise.
    2026-08-31 lesson: steady RMS cannot tell a limit cycle from chaos;
    this can (Matt's sweep-1 verdict - noise that coalesces to pitch only
    in the release)."""
    a, b = int(0.5 * sr), int(1.5 * sr)
    x = sig[a:b] if len(sig) > b else sig[len(sig) // 4: 3 * len(sig) // 4]
    x = x - x.mean()
    if len(x) < 4096 or np.abs(x).max() < 1e-4:
        return 0.0
    f = np.fft.rfft(x, 2 * len(x))
    ac = np.fft.irfft(f * np.conj(f))[:len(x)]
    if ac[0] <= 0:
        return 0.0
    ac /= ac[0]
    lo, hi = int(sr / 1200), int(sr / 30)
    hi = min(hi, len(ac) - 1)
    return float(ac[lo:hi].max()) if hi > lo else 0.0


def wav_peak(path):
    w = wave.open(path)
    n = w.getnframes()
    raw = struct.unpack("<%dh" % (n * w.getnchannels()), w.readframes(n))
    w.close()
    return max(abs(s) for s in raw) / 32767.0 if raw else 0.0


def main():
    lib_dir, lib_npz, patch_dir, wav_dir, pending, top_k = (
        sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4], sys.argv[5],
        int(sys.argv[6]) if len(sys.argv) > 6 else 15)

    if not os.path.exists(lib_npz):
        wavs = nov._collect_wavs(lib_dir)
        print(f"building library from {len(wavs)} WAVs...")
        M, mean, std, names = nov.build_library(wavs)
        nov.save_library(lib_npz, M, mean, std, names)
    M, mean, std, names = nov.load_library(lib_npz)

    man_path = os.path.join(patch_dir, "manifest.json")
    man = json.load(open(man_path))
    dead = scored = 0
    for v in man["variants"]:
        wp = os.path.join(wav_dir, v["file"])
        if not os.path.exists(wp):
            v["status"] = "RENDER_FAIL"
            continue
        sig, sr = wav_mono(wp)
        peak = float(np.abs(sig).max())
        v["peak"] = round(peak, 4)
        if peak < PEAK_FLOOR:
            v["status"] = "DEAD"        # never bloomed - curve gain too low
            dead += 1
            continue
        per = pitchedness(sig, sr)
        v["periodicity"] = round(per, 3)
        vec = emb.embed_file(wp)
        near, cen, idx = nov.novelty(vec, M, mean, std)
        v["status"] = "OK" if per >= PERIODIC_FLOOR else "CHAOS"
        v["novelty"] = round(near, 4)
        v["novelty_centroid"] = round(cen, 4)
        v["novelty_nearest"] = names[idx]
        scored += 1
    # Rank: periodic cells by novelty first, then the chaos pile by novelty —
    # "novel among periodic" (Matt's sweep-1 verdict: novelty alone crowns
    # noise; the baselines' perceived quality lives on the periodic side).
    man["variants"].sort(
        key=lambda v: (v.get("status") == "OK", v.get("novelty", -1)),
        reverse=True)
    json.dump(man, open(man_path, "w"), indent=1)

    os.makedirs(pending, exist_ok=True)
    n_ok = sum(1 for v in man["variants"] if v.get("status") == "OK")
    n_chaos = sum(1 for v in man["variants"] if v.get("status") == "CHAOS")
    sweep_rel = os.path.relpath(patch_dir, ROOT).replace("\\", "/")

    def describe(v):
        p = v["params"]
        return (
            f"- **{v['id']}**  novelty {v['novelty']:.2f}, "
            f"periodicity {v.get('periodicity', 0):.2f} "
            f"(nearest: {v['novelty_nearest']}, peak {v['peak']:.2f}) — "
            f"{p.get('family', '?')}, slope0 {p['slope0']:.1f}, "
            f"smooth {p['smoothness']}, drive {p['drive_sustain']:.2f}, "
            f"ratio {p['ratio']:.2f}, note {p['note']}, "
            f"pos(sh {p['pos']['shoulder']:.2f}, humps {p['pos']['humps']}"
            f"{', FOLD' if p['pos']['fold'] else ''}), "
            f"{'odd-sym' if p['odd_sym'] else 'asym'}"
            f"{', dead-zone' if p['dead_zone'] else ''} — "
            f"patch {sweep_rel}/{v['patch']}")

    lines = ["# Feedback curve sweep — top picks, novel-among-periodic",
             f"(library = {len(names)} sounds; {n_ok} periodic, {n_chaos} "
             f"chaotic, {dead} dead; periodicity floor {PERIODIC_FLOOR}; "
             f"master seed {man['master_seed']})", ""]
    copied = 0
    for v in man["variants"]:
        if v.get("status") != "OK" or copied >= top_k:
            continue
        shutil.copy2(os.path.join(wav_dir, v["file"]),
                     os.path.join(pending, v["file"]))
        lines.append(describe(v))
        copied += 1
    chaos = [v for v in man["variants"] if v.get("status") == "CHAOS"][:3]
    if chaos:
        lines += ["", "## Chaos honorable mentions (not copied — for the "
                      "record)", ""]
        lines += [describe(v) for v in chaos]
    with open(os.path.join(pending, "TOP.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{n_ok} periodic, {n_chaos} chaotic, {dead} dead; "
          f"top {copied} -> {pending}")
    for ln in lines[3:9]:
        print(ln)


main()
