"""gen_segment_sweep2.py — round 2 of the one-shot waveform survey.

Designed entirely from Matt's area-by-area round-1 verdicts:
docs/research/oneshot_sweep/ROUND1_VERDICTS.md (2026-08-23). Highlights:
X-then-thump is the standout construction; scrape shorter with slips growing
fatter + the buzzy-regular corner; bounce longer/chunkier/reversed; dots
shorter with "first half of hump"; clusters vary the two scheduled farts and
pair with thumps; twohit longer with wider thumps/kas; kick corner from wide
atoms; crunch grain trajectories; drop_settle gets a real family;
hold_then_snap variants; NEW family "jagged" (noise condensing into a clean
ramp and back). Dropped: fixed_fine_zct corner ("earsplitting"), plain
rulebreakers.

Reuses round-1 helpers (atom/place/cluster/dots/scrape/crunch/bounce/Shape).
Output: patches/audition/segment_sweep2/ + renders/dsp/pending/segment_sweep2/.
Usage: python tools/gen_segment_sweep2.py [--no-render]
"""
import argparse, json, subprocess
from pathlib import Path
import numpy as np

import gen_segment_sweep as g1
from gen_segment_sweep import (Shape, atom, place, cluster, dots, scrape, crunch,
                               bounce, env_curve, n_of, SR, TAIL)

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH_DIR = ROOT / "patches/audition/segment_sweep2"
RENDER_DIR = ROOT / "renders/dsp/pending/segment_sweep2"

SHAPES = []
def add(sh): SHAPES.append(sh)

# ---------------------------------------------------------------------------
# helpers new in round 2
# ---------------------------------------------------------------------------
def thump(width_s=0.03, peak=1.0, shape="sharp", power=2.0):
    return atom(width_s, peak, shape, power)

def seq(*bufs_gaps):
    """Alternating (buf, gap_s, buf, gap_s, ...) -> concatenated with gaps."""
    ev, t = [], 0.0
    it = iter(bufs_gaps)
    for b in it:
        ev.append((t, b))
        t += len(b) / SR
        g = next(it, None)
        if g is None: break
        t += g
    return place(ev)

def scrape2(total_s, rate, cv=0.15, slip_w0=0.0004, slip_w1=None, press=1.0,
            peak=0.5, seed=None, coupled=False, walk=False, power=1.2):
    """Round-2 scrape: near-regular intervals (jittered by cv), optional slip
    width SCHEDULE w0->w1 across the train (Matt: start narrow, GROW fatter),
    optional slip-interval coupling and surface random-walk."""
    r = np.random.default_rng(seed)
    ev, t, sign = [], 0.0, 1.0
    w_cur, p_cur = slip_w0, peak
    while t < total_s:
        x = t / total_s
        if slip_w1 is not None:
            w = slip_w0 * (slip_w1 / slip_w0) ** x
        elif walk:
            w_cur = float(np.clip(w_cur * np.exp(r.normal(0, 0.10)), 0.0002, 0.01))
            w = w_cur
        else:
            w = slip_w0
        if walk:
            p_cur = float(np.clip(p_cur * np.exp(r.normal(0, 0.12)), 0.1, 1.2))
            pk = p_cur * press
        else:
            pk = peak * press * (0.7 + 0.6 * r.random())
        sign = -sign
        ev.append((t, atom(w, pk, "saw", power) * sign))
        base = 1.0 / rate
        if coupled:
            # relaxation: bigger slip -> longer re-stick -> bigger next slip
            dt = base * (0.4 + 1.6 * pk / max(peak * press, 1e-6))
            dt *= (1 + cv * r.normal())
        else:
            dt = base * (1 + cv * r.normal())
        t += max(dt, 2.0 / SR)
    return place(ev)

def bounce2(t_int, r_int, r_peak, atom_w, shape="tri", two_comp=False,
            reverse=False, rev_peaks_only=False, max_n=40, min_int=0.004):
    ev, t, dt, pk, i = [], 0.0, t_int, 1.0, 0
    hits = []
    while dt > min_int and i < max_n:
        hits.append((t, dt, pk)); t += dt; dt *= r_int; pk *= r_peak; i += 1
    if reverse:
        total = t
        hits = [(total - h[0] - h[1], h[1], h[2]) for h in hits]
        hits.sort()
        base = -min(h[0] for h in hits)
        hits = [(h[0] + base, h[1], h[2]) for h in hits]
    if rev_peaks_only:
        pks = sorted(h[2] for h in hits)
        hits = [(h[0], h[1], pks[i]) for i, h in enumerate(hits)]
    for t0, _, p in hits:
        if two_comp:
            ev.append((t0, atom(atom_w, p * 0.9, "tri")))          # body thump
            ev.append((t0, atom(0.0008, p, "sharp", 2.0)))          # tick
        else:
            ev.append((t0, atom(atom_w, p, shape, 2.0)))
    return place(ev)

def jagged(up_s, down_s, depth=1.0, noise="white", noise_hz=2500, power=1.0,
           peak=1.0, seed=None):
    """Matt's new family: noise condenses into a clean ramp to the peak
    (jaggedness 1->0 rising); reverse on the way down."""
    r = np.random.default_rng(seed)
    def leg(n, rising):
        if n <= 0: return np.zeros(0, dtype=np.float32)
        x = np.linspace(0.0, 1.0, n, endpoint=False)
        ramp = (x ** power if rising else (1.0 - x) ** power) * peak
        j = (1.0 - x) if rising else x
        if noise == "white":
            nz = r.uniform(-1, 1, n)
        else:  # coarse connect-the-dots noise, linear interp
            m = max(2, int(n / (SR / noise_hz)))
            nz = np.interp(np.arange(n), np.linspace(0, n, m + 1), r.uniform(-1, 1, m + 1))
        return (ramp + depth * peak * j * nz).astype(np.float32)
    return place([(0, np.concatenate([leg(n_of(up_s), True), leg(n_of(down_s), False)]))])

def dots2(total_s, w0, w1, sched, **kw):
    """Adds the 'halfhump' schedule: hump's rising half, stopped at widest."""
    import math
    pairs = dots(total_s, w0, w1, "fixed", **kw)  # widths rewritten below
    t, out = 0.0, []
    total = n_of(total_s)
    for w, v in pairs:
        x = min(t / total, 1.0)
        if sched == "halfhump":
            wn = w0 * (w1 / w0) ** math.sin(math.pi * x / 2.0)
        elif sched == "grow":
            wn = w0 * (w1 / w0) ** x
        else:
            wn = w0
        wn = max(1, int(round(wn)))
        out.append((wn, v)); t += wn
        if t >= total: break
    return out

# ---------------------------------------------------------------------------
# families
# ---------------------------------------------------------------------------
def fam_xthump():
    th_s = thump(0.02, 1.0); th_f = thump(0.035, 1.0, "tri")
    scr_reg = lambda s, d=0.35: scrape2(d, 90, 0.15, 0.0008, peak=0.45, seed=s)
    scr_gam = lambda s, d=0.35: scrape(d, 180, "const", peak=0.4, grain="gamma", seed=s)
    scr_acc = lambda s, d=0.4: scrape(d, 140, "accel", peak=0.45, grain="exp", seed=s)
    cells = [
        ("scrape_reg_thump_tight",  seq(scr_reg(1), 0.01, th_s)),
        ("scrape_reg_thump_gap",    seq(scr_reg(2), 0.05, th_s)),
        ("scrape_reg_fatthump",     seq(scr_reg(3), 0.01, th_f)),
        ("scrape_gamma_thump",      seq(scr_gam(4), 0.012, th_s)),
        ("scrape_accel_thump",      seq(scr_acc(5), 0.008, th_s)),
        ("scrape_accel_bigthump",   seq(scr_acc(6), 0.008, thump(0.05, 1.0, "tri"))),
        ("thump_scrape",            seq(th_s, 0.015, scr_reg(7, 0.3))),
        ("scrape_thump_scrape",     seq(scr_reg(8, 0.22), 0.01, th_s, 0.02, scr_reg(9, 0.25) * 0.6)),
        ("cluster_leadin_thump",    seq(cluster(6, 0.002, 0.03, "leadin", 0.0, "shrink", "tri"), 0.012, th_s)),
        ("cluster_leadin_fatthump", seq(cluster(8, 0.002, 0.022, "leadin", 0.1, "shrink", "tri"), 0.01, th_f)),
        ("thump_cluster_tailoff",   seq(th_s, 0.015, cluster(6, 0.002, 0.03, "tailoff", 0.0, "grow", "tri"))),
        ("fatthump_cluster_tailoff", seq(th_f, 0.02, cluster(8, 0.002, 0.024, "tailoff", 0.1, "grow", "tri"))),
    ]
    for nm, buf in cells: add(Shape("xthump", nm, "samples", buf))

def fam_scrape2():
    add(Shape("scrape2", "growfat_020", "samples", scrape2(0.20, 150, 0.3, 0.0003, 0.006, seed=20)))
    add(Shape("scrape2", "growfat_035", "samples", scrape2(0.35, 120, 0.3, 0.0003, 0.008, seed=21)))
    add(Shape("scrape2", "growfat_press", "samples", scrape2(0.30, 100, 0.3, 0.0003, 0.010, press=1.6, seed=22)))
    for rate in (60, 90, 140):
        for cv in (0.10, 0.20):
            add(Shape("scrape2", f"buzz_r{rate}_cv{int(cv*100)}", "samples",
                      scrape2(0.30, rate, cv, 0.0008, peak=0.5, seed=rate + int(cv * 100))))
    add(Shape("scrape2", "coupled", "samples", scrape2(0.35, 110, 0.25, 0.0008, peak=0.5, seed=30, coupled=True)))
    add(Shape("scrape2", "surface_walk", "samples", scrape2(0.35, 110, 0.2, 0.0008, peak=0.5, seed=31, walk=True)))

def fam_bounce2():
    add(Shape("bounce2", "long_slow_chunky", "samples", bounce2(0.22, 0.88, 0.82, 0.008)))
    add(Shape("bounce2", "long_slow_chunkier", "samples", bounce2(0.25, 0.90, 0.85, 0.014, "sine")))
    add(Shape("bounce2", "long_twocomp", "samples", bounce2(0.20, 0.87, 0.82, 0.010, two_comp=True)))
    add(Shape("bounce2", "med_twocomp", "samples", bounce2(0.12, 0.78, 0.75, 0.008, two_comp=True)))
    add(Shape("bounce2", "reverse_full", "samples", bounce2(0.18, 0.82, 0.78, 0.008, reverse=True)))
    add(Shape("bounce2", "reverse_full_long", "samples", bounce2(0.24, 0.90, 0.85, 0.012, "sine", reverse=True)))
    add(Shape("bounce2", "reverse_peaks_only", "samples", bounce2(0.16, 0.80, 0.75, 0.008, rev_peaks_only=True)))
    add(Shape("bounce2", "reverse_peaks_twocomp", "samples", bounce2(0.18, 0.84, 0.78, 0.010, two_comp=True, rev_peaks_only=True)))

def fam_dots2():
    cells = [
        ("halfhump_120ms", 0.12, 1, 900,  "halfhump", 0.5),
        ("halfhump_200ms", 0.20, 1, 1600, "halfhump", 0.5),
        ("halfhump_200ms_s1", 0.20, 1, 1600, "halfhump", 1.0),
        ("halfhump_350ms_wide", 0.35, 2, 2600, "halfhump", 0.5),
        ("grow_150ms", 0.15, 1, 1200, "grow", 0.5),
        ("grow_150ms_s0", 0.15, 1, 1200, "grow", 0.0),
        ("grow_250ms_boost", 0.25, 2, 1600, "grow", 0.5),
        ("halfhump_boost_zct", 0.18, 2, 1200, "halfhump", 0.5),
    ]
    for i, (nm, tot, w0, w1, sc, sm) in enumerate(cells):
        kw = {}
        if "boost" in nm: kw["boost"] = 0.6
        if "zct" in nm: kw["zct"] = 0.8
        add(Shape("dots2", nm, "pairs", dots2(tot, w0, w1, sc, seed=300 + i, **kw), sm, width_unit="samples"))

def fam_clusters2():
    cells = [
        ("leadin_shrink_n6_wide",  6, 0.003, 0.040, "leadin",  "shrink", "tri"),
        ("leadin_shrink_n10",     10, 0.002, 0.024, "leadin",  "shrink", "tri"),
        ("leadin_shrink_sharp",    6, 0.002, 0.030, "leadin",  "shrink", "sharp"),
        ("leadin_shrink_deep",     8, 0.004, 0.034, "leadin",  "shrink", "tri"),
        ("tailoff_grow_n6_wide",   6, 0.003, 0.040, "tailoff", "grow",  "tri"),
        ("tailoff_grow_n10",      10, 0.002, 0.024, "tailoff", "grow",  "tri"),
        ("tailoff_grow_sharp",     6, 0.002, 0.030, "tailoff", "grow",  "sharp"),
        ("both_shrink_then_grow",  12, 0.002, 0.026, "both",   "shrink", "tri"),
    ]
    for nm, n, w, sp, env, ws, sh in cells:
        add(Shape("clusters2", nm, "samples", cluster(n, w, sp, env, 0.0, ws, sh),
                  n=n, atom_w=w, spacing=sp, env=env, wsched=ws, shape=sh))

def fam_twohit2():
    for tw in (0.04, 0.08, 0.15):
        for g in (0.12, 0.22):
            twl = int(tw * 1000); gl = int(g * 1000)
            add(Shape("twohit2", f"heel_toe_t{twl}_g{gl}", "samples",
                      seq(atom(tw, 0.7, "tri"), g, atom(tw * 0.6, 0.9, "tri")),
                      thump_ms=twl, gap_ms=gl))
    for kw in (0.002, 0.006, 0.012):
        add(Shape("twohit2", f"thump_ka_k{int(kw*1000)}ms", "samples",
                  seq(atom(0.08, 0.7, "tri"), 0.15, atom(kw, 1.0, "sharp", 2.0)),
                  ka_ms=kw * 1000))

def fam_kick():
    for w in (0.012, 0.02, 0.035, 0.06, 0.09):
        wl = int(w * 1000)
        add(Shape("kick", f"sharp_p2_{wl}ms", "samples", place([(0, atom(w, 1.0, "sharp", 2.0))]), width=w))
        add(Shape("kick", f"tri_{wl}ms", "pairs", [(w / 2, 1.0), (w / 2, 0.0)], 0.9, width=w))
    add(Shape("kick", "bipolar_tri_35ms", "samples", place([(0, atom(0.035, 1.0, "tri", polarity=0))])))
    add(Shape("kick", "bipolar_sine_60ms", "samples", place([(0, atom(0.06, 1.0, "sine", polarity=0))])))

def fam_texture():
    # crunch grain trajectories: micro-atom width (and density inverse) swept
    def crunch_traj(total_s, w_a, w_b, seed):
        r = np.random.default_rng(seed)
        ev, t = [], 0.0
        while t < total_s:
            x = t / total_s
            w = w_a * (w_b / w_a) ** x
            d = 0.35 / max(w, 1e-4)           # coarser grains -> sparser
            pk = r.uniform(0.25, 1.0) * (0.5 + 0.5 * np.sin(np.pi * min(x, 1.0)) ** 0.5)
            sign = 1.0 if r.random() < 0.5 else -1.0
            ev.append((t, atom(w, pk, "sharp", 1.5) * sign))
            t += r.exponential(1.0 / d)
        return place(ev)
    add(Shape("texture", "crunch_coarse2fine", "samples", crunch_traj(0.35, 0.0025, 0.0002, 40)))
    add(Shape("texture", "crunch_fine2coarse", "samples", crunch_traj(0.35, 0.0002, 0.0025, 41)))
    add(Shape("texture", "crunch_c2f_long", "samples", crunch_traj(0.6, 0.003, 0.0002, 42)))
    add(Shape("texture", "crunch_f2c_long", "samples", crunch_traj(0.6, 0.0002, 0.003, 43)))
    # drop_settle family: bounce half x settle half
    b_fast = bounce2(0.09, 0.68, 0.62, 0.006)
    b_slow = bounce2(0.16, 0.84, 0.78, 0.010)
    s_fine = crunch(2, 0.05, 0.03, 1200, seed=50, w_hi=0.0005)
    s_coarse = crunch(2, 0.06, 0.04, 400, seed=51, w_lo=0.0008, w_hi=0.003)
    add(Shape("texture", "drop_fast_settle_fine", "samples", seq(b_fast, 0.03, s_fine * 0.5)))
    add(Shape("texture", "drop_fast_settle_coarse", "samples", seq(b_fast, 0.03, s_coarse * 0.5)))
    add(Shape("texture", "drop_slow_settle_fine", "samples", seq(b_slow, 0.05, s_fine * 0.45)))
    add(Shape("texture", "drop_slow_settle_coarse", "samples", seq(b_slow, 0.05, s_coarse * 0.45)))
    add(Shape("texture", "revbounce_drop_settle", "samples",
              seq(bounce2(0.12, 0.78, 0.72, 0.008, reverse=True), 0.02, s_coarse * 0.5)))
    # hold_then_snap variants + fresh rule-breaks
    add(Shape("texture", "holdsnap_60ms", "pairs", [(0.0002, 1.0), (0.06, 1.0), (0.0002, 0.0)], 0.0))
    add(Shape("texture", "holdsnap_400ms", "pairs", [(0.0002, 1.0), (0.4, 1.0), (0.0002, 0.0)], 0.0))
    add(Shape("texture", "holdsnap_ramp_in", "pairs", [(0.03, 1.0), (0.15, 1.0), (0.0002, 0.0)], 0.5))
    add(Shape("texture", "holdsnap_double", "pairs",
              [(0.0002, 1.0), (0.08, 1.0), (0.0002, 0.0), (0.05, 0.0), (0.0002, -1.0), (0.08, -1.0), (0.0002, 0.0)], 0.0))
    add(Shape("texture", "silence_three_spikes", "samples",
              place([(0.05, atom(0.0004, 1.0, "sharp", 2.0)), (0.31, atom(0.0004, 0.9, "sharp", 2.0)),
                     (0.72, atom(0.0004, 1.0, "sharp", 2.0))])))
    add(Shape("texture", "dc_square_flip", "pairs", [(0.0002, 0.8), (0.12, 0.8), (0.0004, -0.8), (0.12, -0.8), (0.0004, 0.0)], 0.0))

def fam_jagged():
    cells = [
        ("in_white_300ms",       0.30, 0.0,  1.0, "white", 2500, 1.0),
        ("in_white_deep_600ms",  0.60, 0.0,  1.0, "white", 2500, 2.0),
        ("in_coarse_300ms",      0.30, 0.0,  1.0, "coarse", 900, 1.0),
        ("in_coarse_shallow",    0.40, 0.0,  0.5, "coarse", 1800, 1.0),
        ("out_white_400ms",      0.0,  0.40, 1.0, "white", 2500, 1.0),
        ("out_coarse_600ms",     0.0,  0.60, 1.0, "coarse", 900, 1.5),
        ("both_300_300",         0.30, 0.30, 1.0, "white", 2500, 1.0),
        ("both_coarse_500_700",  0.50, 0.70, 0.8, "coarse", 1200, 1.5),
        ("both_fast_120_180",    0.12, 0.18, 1.0, "white", 2500, 1.0),
    ]
    for i, (nm, up, dn, dep, nk, nhz, pw) in enumerate(cells):
        add(Shape("jagged", nm, "samples", jagged(up, dn, dep, nk, nhz, pw, seed=600 + i),
                  up_s=up, down_s=dn, depth=dep, noise=nk, noise_hz=nhz, power=pw))

# ---------------------------------------------------------------------------
def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-render", action="store_true")
    args = ap.parse_args()
    for f in (fam_xthump, fam_scrape2, fam_bounce2, fam_dots2, fam_clusters2,
              fam_twohit2, fam_kick, fam_texture, fam_jagged):
        f()
    manifest, fails = [], 0
    for i, sh in enumerate(SHAPES):
        pdir = PATCH_DIR / sh.family; pdir.mkdir(parents=True, exist_ok=True)
        rdir = RENDER_DIR / sh.family; rdir.mkdir(parents=True, exist_ok=True)
        pj = g1.patch_json(sh, 2000 + i)
        ppath = pdir / f"{sh.name}.json"
        ppath.write_text(json.dumps(pj, indent=1), encoding="utf-8")
        wpath = rdir / f"{sh.name}.wav"
        entry = {"family": sh.family, "name": sh.name, "kind": sh.kind,
                 "duration_s": round(sh.duration(), 4), "n_values": len(sh.values()),
                 "smoothness": sh.smoothness, "patch": str(ppath.relative_to(ROOT)),
                 "wav": str(wpath.relative_to(ROOT)),
                 "meta": {k: (v if isinstance(v, (int, float, str, bool)) else str(v)) for k, v in sh.meta.items()}}
        if not args.no_render:
            r = subprocess.run([str(CLI), str(ppath), str(wpath)], capture_output=True, text=True)
            entry["render_ok"] = (r.returncode == 0)
            if r.returncode != 0:
                fails += 1; entry["error"] = (r.stderr or r.stdout).strip()[-200:]
        manifest.append(entry)
    RENDER_DIR.mkdir(parents=True, exist_ok=True)
    (RENDER_DIR / "manifest.json").write_text(json.dumps(manifest, indent=1), encoding="utf-8")
    by = {}
    for e in manifest: by[e["family"]] = by.get(e["family"], 0) + 1
    print(f"{len(SHAPES)} shapes: " + ", ".join(f"{k} {v}" for k, v in by.items()) + f"; render fails {fails}")

if __name__ == "__main__":
    main()
