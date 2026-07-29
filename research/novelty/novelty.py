"""Novelty metric (dsp BACKLOG item 4) — score how UNLIKE the existing patch
library a sound is, so exploration sweeps can self-rank toward the unfamiliar.

Pipeline:
  1. build  — embed a set of library WAVs, z-score-normalise the feature space,
              store the normaliser + library matrix in an .npz.
  2. score  — embed candidate WAV(s), report novelty = distance to the nearest
              library neighbour (primary) and to the library centroid, both in
              the z-scored space. Higher = more novel.
  3. manifest — augment an --explore manifest.json: render paths -> novelty,
              re-rank variants by novelty (the "wire into --explore-filter"
              goal, done Python-side since the C++ filter has no library ref).

Self-verifiable: `python novelty.py selftest` builds a synthetic library and
asserts identity->0, a near-neighbour scores low, and out-of-distribution
signals (noise, extreme timbres) score strictly higher.

Usage:
  python novelty.py build  <lib_glob_or_dir> <library.npz>
  python novelty.py score  <library.npz> <cand.wav | cand_dir> [--limit N]
  python novelty.py manifest <library.npz> <manifest.json> [--wav-key file]
  python novelty.py selftest
"""
import glob
import json
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import embedding as emb


# ------------------------------------------------------------------ library ---
def _collect_wavs(arg):
    if os.path.isdir(arg):
        return sorted(glob.glob(os.path.join(arg, "**", "*.wav"), recursive=True))
    return sorted(glob.glob(arg))


def build_library(wav_paths):
    """Embed all WAVs; return (matrix, mean, std, names). mean/std z-score."""
    vecs, names = [], []
    for p in wav_paths:
        try:
            vecs.append(emb.embed_file(p))
            names.append(os.path.basename(p))
        except Exception as ex:              # noqa: BLE001 - report and skip
            print(f"  skip {p}: {ex}", file=sys.stderr)
    if not vecs:
        raise RuntimeError("no WAVs embedded for library")
    M = np.vstack(vecs)
    mean = M.mean(axis=0)
    std = M.std(axis=0)
    std[std < 1e-9] = 1.0                    # guard constant dims
    return M, mean, std, names


def save_library(path, M, mean, std, names):
    np.savez(path, matrix=M, mean=mean, std=std,
             names=np.array(names), feature_names=np.array(emb.FEATURE_NAMES))


def load_library(path):
    d = np.load(path, allow_pickle=True)
    return d["matrix"], d["mean"], d["std"], list(d["names"])


# ------------------------------------------------------------------ scoring ---
def novelty(vec, M, mean, std):
    """Return (nearest_dist, centroid_dist, nearest_index) in z-scored space."""
    z = (vec - mean) / std
    Mz = (M - mean) / std
    d = np.linalg.norm(Mz - z[None, :], axis=1)
    centroid_d = float(np.linalg.norm(Mz.mean(axis=0) - z))
    return float(d.min()), centroid_d, int(d.argmin())


def score_paths(lib_npz, cand_arg, limit=None):
    M, mean, std, names = load_library(lib_npz)
    cands = _collect_wavs(cand_arg) if (os.path.isdir(cand_arg)
                                        or any(c in cand_arg for c in "*?")) \
        else [cand_arg]
    rows = []
    for p in cands:
        try:
            v = emb.embed_file(p)
        except Exception as ex:              # noqa: BLE001
            print(f"  skip {p}: {ex}", file=sys.stderr)
            continue
        near, cen, idx = novelty(v, M, mean, std)
        rows.append((os.path.basename(p), near, cen, names[idx]))
    rows.sort(key=lambda r: r[1], reverse=True)
    if limit:
        rows = rows[:limit]
    return rows


def augment_manifest(lib_npz, manifest_path, wav_key="file"):
    M, mean, std, names = load_library(lib_npz)
    with open(manifest_path) as f:
        man = json.load(f)
    variants = man.get("variants", man if isinstance(man, list) else [])
    base = os.path.dirname(os.path.abspath(manifest_path))
    for v in variants:
        wav = v.get(wav_key) or v.get("wav") or v.get("path")
        if not wav:
            continue
        wp = wav if os.path.isabs(wav) else os.path.join(base, wav)
        if not os.path.exists(wp):
            continue
        vec = emb.embed_file(wp)
        near, cen, idx = novelty(vec, M, mean, std)
        v["novelty"] = round(near, 4)
        v["novelty_centroid"] = round(cen, 4)
        v["novelty_nearest"] = names[idx]
    variants.sort(key=lambda v: v.get("novelty", -1), reverse=True)
    return man


# ------------------------------------------------------------------ selftest --
def _synth(kind, sr=emb.SR_REF, dur=1.0, f0=440.0, seed=0):
    t = np.arange(int(sr * dur)) / sr
    rng = np.random.default_rng(seed)
    if kind == "sine":
        return np.sin(2 * np.pi * f0 * t)
    if kind == "saw":
        return 2 * (t * f0 - np.floor(0.5 + t * f0))
    if kind == "square":
        return np.sign(np.sin(2 * np.pi * f0 * t))
    if kind == "noise":
        return rng.standard_normal(len(t))
    if kind == "chirp":
        return np.sin(2 * np.pi * (f0 + (8000 - f0) * t / dur / 2) * t)
    raise ValueError(kind)


def selftest():
    sr = emb.SR_REF
    # Library: a family of tonal timbres around 300-600 Hz.
    lib_specs = [("sine", 440), ("sine", 330), ("sine", 550),
                 ("saw", 440), ("saw", 330), ("square", 440)]
    M = np.vstack([emb.embed(_synth(k, sr, f0=f), sr) for k, f in lib_specs])
    mean = M.mean(axis=0); std = M.std(axis=0); std[std < 1e-9] = 1.0

    def nov(sig):
        return novelty(emb.embed(sig, sr), M, mean, std)[0]

    identity = nov(_synth("sine", sr, f0=440))          # exact library member
    near = nov(_synth("sine", sr, f0=470))              # interpolates library
    noise = nov(_synth("noise", sr, seed=1))            # out of distribution
    chirp = nov(_synth("chirp", sr))                    # non-stationary, OOD

    print(f"  identity (sine440, in library):   {identity:.4f}")
    print(f"  near     (sine470, interpolated): {near:.4f}")
    print(f"  chirp    (sweep, OOD):            {chirp:.4f}")
    print(f"  noise    (white, OOD):            {noise:.4f}")

    ok = True
    checks = [
        ("identity ~ 0", identity < 1e-6),
        ("near > identity", near > identity),
        # Both out-of-distribution signals must land far beyond a mere
        # near-neighbour — that ordering (novel >> familiar) is the whole
        # point of the metric. Which OOD signal ranks #1 is not asserted.
        ("noise >> near", noise > 10 * near),
        ("chirp >> near", chirp > 10 * near),
    ]
    for label, cond in checks:
        print(f"    [{'PASS' if cond else 'FAIL'}] {label}")
        ok = ok and cond
    print("SELFTEST", "PASS" if ok else "FAIL")
    return 0 if ok else 1


# ---------------------------------------------------------------------- main --
def main(argv):
    if not argv:
        print(__doc__)
        return 2
    cmd = argv[0]
    if cmd == "selftest":
        return selftest()
    if cmd == "build":
        wavs = _collect_wavs(argv[1])
        print(f"embedding {len(wavs)} library WAVs...")
        M, mean, std, names = build_library(wavs)
        save_library(argv[2], M, mean, std, names)
        print(f"library -> {argv[2]}  ({len(names)} sounds, dim={emb.EMBED_DIM})")
        return 0
    if cmd == "score":
        limit = None
        if "--limit" in argv:
            limit = int(argv[argv.index("--limit") + 1])
        rows = score_paths(argv[1], argv[2], limit)
        print(f"{'novelty':>9} {'centroid':>9}  candidate  (nearest library)")
        for name, near, cen, nearest in rows:
            print(f"{near:9.4f} {cen:9.4f}  {name}  ({nearest})")
        return 0
    if cmd == "manifest":
        wav_key = "file"
        if "--wav-key" in argv:
            wav_key = argv[argv.index("--wav-key") + 1]
        man = augment_manifest(argv[1], argv[2], wav_key)
        with open(argv[2], "w") as f:
            json.dump(man, f, indent=2)
        print(f"augmented {argv[2]} with novelty scores")
        return 0
    print(f"unknown command: {cmd}\n{__doc__}")
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
