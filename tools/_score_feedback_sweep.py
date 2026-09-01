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
        peak = wav_peak(wp)
        v["peak"] = round(peak, 4)
        if peak < PEAK_FLOOR:
            v["status"] = "DEAD"        # never bloomed - curve gain too low
            dead += 1
            continue
        vec = emb.embed_file(wp)
        near, cen, idx = nov.novelty(vec, M, mean, std)
        v["status"] = "OK"
        v["novelty"] = round(near, 4)
        v["novelty_centroid"] = round(cen, 4)
        v["novelty_nearest"] = names[idx]
        scored += 1
    man["variants"].sort(key=lambda v: v.get("novelty", -1), reverse=True)
    json.dump(man, open(man_path, "w"), indent=1)

    os.makedirs(pending, exist_ok=True)
    lines = ["# Feedback curve sweep r1 — top picks by novelty",
             f"(library = {len(names)} sounds; {scored} scored, {dead} dead, "
             f"master seed {man['master_seed']})", ""]
    for v in man["variants"][:top_k]:
        if v.get("status") != "OK":
            continue
        shutil.copy2(os.path.join(wav_dir, v["file"]),
                     os.path.join(pending, v["file"]))
        p = v["params"]
        lines.append(
            f"- **{v['id']}**  novelty {v['novelty']:.2f} "
            f"(nearest: {v['novelty_nearest']}, peak {v['peak']:.2f}) — "
            f"slope0 {p['slope0']:.1f}, smooth {p['smoothness']}, "
            f"drive {p['drive_sustain']:.2f}, ratio {p['ratio']:.2f}, "
            f"note {p['note']}, "
            f"pos(sh {p['pos']['shoulder']:.2f}, humps {p['pos']['humps']}"
            f"{', FOLD' if p['pos']['fold'] else ''}), "
            f"{'odd-sym' if p['odd_sym'] else 'asym'}"
            f"{', dead-zone' if p['dead_zone'] else ''} — "
            f"patch patches/sweep/feedback_curves1/{v['patch']}")
    with open(os.path.join(pending, "TOP.md"), "w") as f:
        f.write("\n".join(lines) + "\n")
    print(f"{scored} scored, {dead} dead; top {top_k} -> {pending}")
    for ln in lines[3:9]:
        print(ln)


main()
