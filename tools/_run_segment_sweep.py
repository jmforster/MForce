"""_run_segment_sweep.py — prune the segment sweep for Matt's ears.

Reads renders/dsp/pending/segment_sweep/manifest.json (from gen_segment_sweep.py),
computes STRUCTURE descriptors per WAV (one-shots are about temporal structure,
which the instrument-shaped novelty embedding does not separate well), dedupes
by audio hash, then picks a diverse set by farthest-point sampling in a
z-scored [structure + timbre-embedding] space — with a per-family floor so no
family is silently dropped. Writes README.md + copies picks to _picks/.

Usage: python tools/_run_segment_sweep.py [--n 40] [--per-family 3]
"""
import argparse, hashlib, json, shutil, sys
from pathlib import Path
import numpy as np
from scipy.io import wavfile
from scipy.signal import stft

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "research" / "novelty"))
from embedding import embed  # noqa: E402

RDIR = ROOT / "renders/dsp/pending/segment_sweep"   # override with --dir
SR = 48000

def load(path):
    sr, d = wavfile.read(path)
    if d.dtype.kind in "iu":
        d = d.astype(np.float32) / np.iinfo(d.dtype).max
    d = d.astype(np.float32)
    if d.ndim > 1:
        d = d.mean(axis=1)
    return sr, d

def descriptors(x, sr):
    a = np.abs(x)
    peak = float(a.max()) if len(a) else 0.0
    if peak <= 0:
        return dict(peak=0, rms=0, crest=0, attack_ms=0, active_ms=0, onsets=0,
                    ioi_mean_ms=0, ioi_cv=0, centroid_hz=0, centroid_first10_hz=0, zcr=0, flat=0)
    # smoothed envelope, 1 ms window
    k = max(1, int(sr * 0.001))
    env = np.convolve(a, np.ones(k) / k, mode="same")
    thr = 0.02 * peak
    active = np.where(env > thr)[0]
    active_ms = 1000.0 * (active[-1] - active[0] + 1) / sr if len(active) else 0.0
    seg = x[active[0]:active[-1] + 1] if len(active) else x
    rms = float(np.sqrt(np.mean(seg ** 2))) if len(seg) else 0.0
    crest = peak / rms if rms > 0 else 0.0
    # attack 10-90 of the cumulative envelope up to the global peak
    ip = int(np.argmax(env))
    pre = env[: ip + 1]
    i10 = int(np.searchsorted(pre, 0.1 * env[ip]))
    i90 = int(np.searchsorted(pre, 0.9 * env[ip]))
    attack_ms = 1000.0 * max(0, i90 - i10) / sr
    # onsets: local maxima of the envelope above 15% of peak, >= 3 ms apart
    md = int(sr * 0.003)
    cand = np.where((env[1:-1] > env[:-2]) & (env[1:-1] >= env[2:]) & (env[1:-1] > 0.15 * env.max()))[0] + 1
    onsets = []
    for c in cand:
        if not onsets or c - onsets[-1] >= md:
            onsets.append(c)
        elif env[c] > env[onsets[-1]]:
            onsets[-1] = c
    ioi = np.diff(onsets) / sr * 1000.0 if len(onsets) > 1 else np.array([])
    ioi_mean = float(ioi.mean()) if len(ioi) else 0.0
    ioi_cv = float(ioi.std() / ioi.mean()) if len(ioi) and ioi.mean() > 0 else 0.0
    # spectral: whole active region, and the first 10 ms
    def centroid(sig):
        if len(sig) < 64: return 0.0
        f, _, Z = stft(sig, fs=sr, nperseg=min(1024, len(sig)))
        m = np.abs(Z).mean(axis=1)
        return float((f * m).sum() / m.sum()) if m.sum() > 0 else 0.0
    cen = centroid(seg)
    cen10 = centroid(seg[: int(sr * 0.010)])
    zcr = float(np.mean(np.abs(np.diff(np.sign(seg))) > 0)) if len(seg) > 1 else 0.0
    if len(seg) >= 64:
        f, _, Z = stft(seg, fs=sr, nperseg=min(1024, len(seg)))
        m = np.abs(Z).mean(axis=1) + 1e-12
        flat = float(np.exp(np.mean(np.log(m))) / m.mean())
    else:
        flat = 0.0
    return dict(peak=peak, rms=rms, crest=crest, attack_ms=attack_ms, active_ms=active_ms,
                onsets=len(onsets), ioi_mean_ms=ioi_mean, ioi_cv=ioi_cv,
                centroid_hz=cen, centroid_first10_hz=cen10, zcr=zcr, flat=flat)

def farthest_point(X, n, must=()):
    """Greedy FPS in z-scored space, seeded with `must` indices."""
    chosen = list(must)
    if not chosen:
        chosen = [int(np.argmax(np.linalg.norm(X - X.mean(0), axis=1)))]
    d = np.min(np.linalg.norm(X[:, None, :] - X[chosen][None, :, :], axis=2), axis=1)
    while len(chosen) < min(n, len(X)):
        i = int(np.argmax(d))
        if d[i] <= 0: break
        chosen.append(i)
        d = np.minimum(d, np.linalg.norm(X - X[i], axis=1))
    return chosen

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=40)
    ap.add_argument("--per-family", type=int, default=3)
    ap.add_argument("--dir", default=None, help="sweep dir name under renders/dsp/pending/")
    args = ap.parse_args()
    global RDIR
    if args.dir: RDIR = ROOT / "renders/dsp/pending" / args.dir
    man = json.loads((RDIR / "manifest.json").read_text(encoding="utf-8"))
    rows, seen = [], {}
    for e in man:
        if not e.get("render_ok", True): continue
        p = ROOT / e["wav"]
        sr, x = load(p)
        h = hashlib.sha256(x.tobytes()).hexdigest()[:12]
        if h in seen:
            e["dup_of"] = seen[h]; continue
        seen[h] = e["name"]
        e["desc"] = descriptors(x, sr)
        e["emb"] = embed(x, sr).tolist() if len(x) >= 2048 else None
        rows.append(e)
    # feature matrix: structure descriptors + embedding (zero-filled if missing)
    keys = ["crest", "attack_ms", "active_ms", "onsets", "ioi_mean_ms", "ioi_cv",
            "centroid_hz", "centroid_first10_hz", "zcr", "flat"]
    S = np.array([[r["desc"][k] for k in keys] for r in rows], dtype=float)
    S[:, 1] = np.log1p(S[:, 1]); S[:, 2] = np.log1p(S[:, 2]); S[:, 4] = np.log1p(S[:, 4])
    S[:, 6] = np.log1p(S[:, 6]); S[:, 7] = np.log1p(S[:, 7])
    edim = len(next(r["emb"] for r in rows if r["emb"]))
    E = np.array([r["emb"] if r["emb"] else [0.0] * edim for r in rows], dtype=float)
    def z(M):
        sd = M.std(0); sd[sd == 0] = 1.0
        return (M - M.mean(0)) / sd
    X = np.hstack([z(S) * 1.5, z(E) * 0.6])   # structure weighted above timbre
    # per-family floor: FPS within each family first
    must = []
    fams = sorted(set(r["family"] for r in rows))
    for f in fams:
        idx = [i for i, r in enumerate(rows) if r["family"] == f]
        sub = farthest_point(X[idx], args.per_family)
        must += [idx[i] for i in sub]
    picks = farthest_point(X, args.n, must=must)
    picks = sorted(set(picks), key=lambda i: (rows[i]["family"], rows[i]["name"]))
    # write outputs
    pdir = RDIR / "_picks"; pdir.mkdir(exist_ok=True)
    for f in pdir.glob("*.wav"): f.unlink()
    lines = [f"# {RDIR.name} — one-shot waveform sweep picks", "",
             f"{len(man)} cells generated, {len(rows)} unique after hash-dedupe "
             f"({len(man)-len(rows)} byte-identical dups dropped), {len(picks)} picked for ears.",
             "Picks = farthest-point sampling in z-scored [structure x1.5 + timbre x0.6] space,",
             f"seeded with {args.per_family} per family. Full set stays in the family folders;",
             "picks are copied to _picks/ as <family>__<name>.wav.",
             "", "| # | family | name | ms | onsets | IOI ms (cv) | attack ms | crest | centroid Hz | first-10ms Hz | note |",
             "|---|---|---|---|---|---|---|---|---|---|---|"]
    for k, i in enumerate(picks, 1):
        r = rows[i]; d = r["desc"]
        shutil.copy(ROOT / r["wav"], pdir / f"{r['family']}__{r['name']}.wav")
        note = "; ".join(f"{a}={b}" for a, b in r["meta"].items() if a not in ("width",))[:60]
        lines.append(f"| {k} | {r['family']} | {r['name']} | {d['active_ms']:.0f} | {d['onsets']} | "
                     f"{d['ioi_mean_ms']:.1f} ({d['ioi_cv']:.2f}) | {d['attack_ms']:.2f} | {d['crest']:.1f} | "
                     f"{d['centroid_hz']:.0f} | {d['centroid_first10_hz']:.0f} | {note} |")
    lines += ["", "## Not picked (still in the folders)", ""]
    for r in rows:
        if rows.index(r) not in picks:
            lines.append(f"- {r['family']}/{r['name']}")
    dups = [e for e in man if "dup_of" in e]
    if dups:
        lines += ["", "## Byte-identical duplicates dropped", ""] + [f"- {e['family']}/{e['name']} == {e['dup_of']}" for e in dups]
    (RDIR / "README.md").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (RDIR / "descriptors.json").write_text(json.dumps(
        [{k: v for k, v in r.items() if k != "emb"} for r in rows], indent=1), encoding="utf-8")
    print(f"{len(rows)} unique / {len(man)}; picked {len(picks)} -> {pdir}")
    from collections import Counter
    print("picks per family:", dict(Counter(rows[i]["family"] for i in picks)))

if __name__ == "__main__":
    main()
