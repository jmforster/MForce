"""gen_segment_sweep.py — round-1 survey of one-shot time-domain waveforms.

Matt, 2026-08-23 (Passport roots brainstorm): "we're looking for interesting
waveforms to *play once*" — arbitrary complexity, not limited to single
pulses; footsteps are TWO hits (heel-toe: ta-tack, ka-thump, cr-crunch);
clusters of pulses like partial groups, lead-in / tail-off energy; scraping
is a priority (stick-slip = the bow lineage); clank is NOT (FM does clanks).

The player is SegmentSource (oneShot=true). Shapes come in two kinds:
  pairs   — native (width, value) segments, widths in SECONDS (< 1), so the
            node's smoothness/varPct machinery still applies;
  samples — a pre-summed sample buffer emitted as width-1 segments (the only
            way to express OVERLAP with the current node).
Nothing here prejudges which engine primitive survives (SegmentSource vs a
one-shot table) — the generator emits arrays; the player is swappable.

Output (both gitignored):
  patches/audition/segment_sweep/<family>/<name>.json   render-style patches
  renders/dsp/pending/segment_sweep/<family>/<name>.wav via mforce_cli
  renders/dsp/pending/segment_sweep/manifest.json       params per cell

Usage: python tools/gen_segment_sweep.py [--no-render] [--family atoms,...]
"""
import argparse, json, math, os, subprocess, sys
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCH_DIR = ROOT / "patches/audition/segment_sweep"
RENDER_DIR = ROOT / "renders/dsp/pending/segment_sweep"
SR = 48000
TAIL = 0.3          # seconds of silence after the shape
rng = np.random.default_rng(20260823)

# ---------------------------------------------------------------------------
# Shape record
# ---------------------------------------------------------------------------
class Shape:
    def __init__(self, family, name, kind, data, smoothness=0.5, width_unit="s", **meta):
        self.family, self.name, self.kind = family, name, kind
        self.data = data                  # pairs: [(w, v)...]  samples: np.array
        self.smoothness = smoothness
        self.width_unit = width_unit      # pairs only: "s" (widths < 1) or "samples" (ints >= 1)
        self.meta = meta

    def duration(self):
        if self.kind == "pairs":
            tot = sum(w for w, _ in self.data)
            return tot / SR if self.width_unit == "samples" else tot
        return len(self.data) / SR

    def values(self):
        if self.kind == "pairs":
            out = []
            for w, v in self.data:
                out += [int(w) if self.width_unit == "samples" else round(float(w), 6),
                        round(float(v), 5)]
            return out
        out = []
        for v in self.data:
            out += [1, round(float(v), 5)]
        return out

SHAPES = []
def add(shape): SHAPES.append(shape)

# ---------------------------------------------------------------------------
# Sample-domain helpers (for 'samples' kind)
# ---------------------------------------------------------------------------
def n_of(sec): return max(1, int(round(sec * SR)))

def atom(width_s, peak=1.0, shape="tri", power=1.0, polarity=1):
    """One pulse, unipolar unless polarity=0 (bipolar: + lobe then - lobe)."""
    n = n_of(width_s)
    t = np.linspace(0.0, 1.0, n, endpoint=False)
    if shape == "tri":
        y = 1.0 - np.abs(2 * t - 1)
    elif shape == "sine":
        y = np.sin(np.pi * t)
    elif shape == "sharp":           # fast attack, power-curved decay
        a = max(1, n // 10)
        y = np.concatenate([np.linspace(0, 1, a, endpoint=False),
                            (1 - np.linspace(0, 1, n - a)) ** power])
    elif shape == "rev":             # slow build, instant drop
        a = max(1, n // 10)
        y = np.concatenate([np.linspace(0, 1, n - a, endpoint=False) ** power,
                            np.linspace(1, 0, a)])
    elif shape == "saw":             # the stick-slip micro-event: build, snap
        y = t ** power
    else:
        raise ValueError(shape)
    y = y * peak
    if polarity == 0:
        y = np.concatenate([y, -y])
    elif polarity < 0:
        y = -y
    return y.astype(np.float32)

def place(events, tail=TAIL):
    """events: list of (t_start_s, array). Sum with overlap into one buffer."""
    end = max(t + len(a) / SR for t, a in events) + tail
    buf = np.zeros(n_of(end), dtype=np.float32)
    for t, a in events:
        i = n_of(t)
        j = min(len(buf), i + len(a))
        buf[i:j] += a[: j - i]
    return buf

def env_curve(n, kind):
    x = np.linspace(0, 1, n)
    if kind == "flat":    return np.ones(n)
    if kind == "leadin":  return x ** 1.5
    if kind == "tailoff": return (1 - x) ** 1.5
    if kind == "both":    return np.sin(np.pi * x) ** 0.8
    raise ValueError(kind)

# ---------------------------------------------------------------------------
# A. atoms — single pulse ladders (native pairs + a few power-curved samples)
# ---------------------------------------------------------------------------
def fam_atoms():
    widths = [0.0003, 0.001, 0.003, 0.01, 0.03, 0.08]
    for w in widths:
        wl = f"{int(w*1000*10)/10:g}ms"
        add(Shape("atoms", f"tri_{wl}", "pairs", [(w/2, 1.0), (w/2, 0.0)], 0.5, shape="tri", width=w))
        add(Shape("atoms", f"sine_{wl}", "pairs", [(w/2, 1.0), (w/2, 0.0)], 1.0, shape="sine", width=w))
        add(Shape("atoms", f"sharp_{wl}", "pairs", [(w*0.1, 1.0), (w*0.9, 0.0)], 0.5, shape="sharp", width=w))
        add(Shape("atoms", f"rev_{wl}", "pairs", [(w*0.9, 1.0), (w*0.1, 0.0)], 0.5, shape="rev", width=w))
    # power-curved decays and bipolar versions (samples)
    for w in [0.001, 0.01, 0.04]:
        wl = f"{int(w*1000*10)/10:g}ms"
        for p in [0.3, 3.0]:
            add(Shape("atoms", f"sharp_pow{p:g}_{wl}", "samples", place([(0, atom(w, 1, "sharp", p))]), shape="sharp", power=p, width=w))
        add(Shape("atoms", f"bipolar_tri_{wl}", "samples", place([(0, atom(w, 1, "tri", polarity=0))]), shape="tri", bipolar=True, width=w))
        add(Shape("atoms", f"bipolar_sharp_{wl}", "samples", place([(0, atom(w, 1, "sharp", 2, polarity=0))]), shape="sharp", bipolar=True, width=w))

# ---------------------------------------------------------------------------
# B. two-hit / three-hit — heel-toe structures (native pairs)
# ---------------------------------------------------------------------------
def pulse_pairs(w, peak, shape="sharp"):
    if shape == "sharp": return [(w*0.1, peak), (w*0.9, 0.0)]
    if shape == "tri":   return [(w/2, peak), (w/2, 0.0)]
    if shape == "rev":   return [(w*0.9, peak), (w*0.1, 0.0)]
    raise ValueError(shape)

def fam_twohit():
    for g in [0.03, 0.06, 0.10, 0.16]:
        gl = f"g{int(g*1000)}"
        # ta-tack: two short bright hits, second slightly shorter/softer
        add(Shape("twohit", f"ta_tack_{gl}", "pairs",
                  pulse_pairs(0.0015, 1.0) + [(g, 0.0)] + pulse_pairs(0.001, 0.8),
                  0.5, pattern="ta-tack", gap=g))
        # ka-thump: short bright then long soft (sine-ish)
        add(Shape("twohit", f"ka_thump_{gl}", "pairs",
                  pulse_pairs(0.001, 1.0) + [(g, 0.0)] + pulse_pairs(0.025, 0.7, "tri"),
                  0.7, pattern="ka-thump", gap=g))
        # thump-ka: reversed order
        add(Shape("twohit", f"thump_ka_{gl}", "pairs",
                  pulse_pairs(0.025, 0.7, "tri") + [(g, 0.0)] + pulse_pairs(0.001, 1.0),
                  0.7, pattern="thump-ka", gap=g))
    # three-hit and uneven
    add(Shape("twohit", "ka_ta_tack", "pairs",
              pulse_pairs(0.001, 0.9) + [(0.04, 0)] + pulse_pairs(0.0015, 1.0) + [(0.07, 0)] + pulse_pairs(0.001, 0.8),
              0.5, pattern="ka-ta-tack"))
    add(Shape("twohit", "tack_thuuump", "pairs",
              pulse_pairs(0.0008, 1.0) + [(0.05, 0)] + pulse_pairs(0.08, 0.6, "rev"),
              0.8, pattern="tack-thuuump"))
    add(Shape("twohit", "heel_toe_soft", "pairs",
              pulse_pairs(0.006, 0.7, "tri") + [(0.09, 0)] + pulse_pairs(0.004, 0.9, "tri"),
              0.9, pattern="heel-toe soft"))

# ---------------------------------------------------------------------------
# C. clusters — N atoms, peak envelope, spacing, width schedule (samples, may overlap)
# ---------------------------------------------------------------------------
def cluster(n, atom_w, spacing, env, jitter=0.0, wsched="const", shape="sharp", polarity=1, peak=1.0):
    ev = []
    t = 0.0
    e = env_curve(n, env)
    for i in range(n):
        if wsched == "shrink":  w = atom_w * (1.0 - 0.8 * i / max(1, n-1))
        elif wsched == "grow":  w = atom_w * (0.2 + 0.8 * i / max(1, n-1))
        else:                   w = atom_w
        ev.append((t, atom(max(w, 0.0001), peak * max(e[i], 0.05), shape, 2.0, polarity)))
        t += spacing * (1.0 + jitter * (rng.random() * 2 - 1))
    return place(ev)

def fam_clusters():
    cells = [
        # n, atom_w, spacing, env, jitter, wsched, shape, polarity, tag
        (4,  0.001, 0.012, "flat",    0.0, "const",  "sharp", 1,  "4_flat"),
        (8,  0.001, 0.012, "leadin",  0.0, "const",  "sharp", 1,  "8_leadin"),
        (8,  0.001, 0.012, "tailoff", 0.0, "const",  "sharp", 1,  "8_tailoff"),
        (8,  0.001, 0.012, "both",    0.0, "const",  "sharp", 1,  "8_both"),
        (16, 0.001, 0.008, "both",    0.0, "const",  "sharp", 1,  "16_both_tight"),
        (16, 0.001, 0.008, "both",    0.4, "const",  "sharp", 1,  "16_both_jitter"),
        (8,  0.004, 0.012, "both",    0.0, "const",  "tri",   1,  "8_both_fat_overlap"),
        (8,  0.004, 0.012, "both",    0.0, "shrink", "tri",   1,  "8_both_shrink"),
        (8,  0.001, 0.012, "leadin",  0.0, "grow",   "sharp", 1,  "8_leadin_grow"),
        (8,  0.001, 0.012, "tailoff", 0.0, "shrink", "sharp", 1,  "8_tailoff_shrink"),
        (12, 0.002, 0.010, "both",    0.3, "const",  "sharp", 0,  "12_both_bipolar"),
        (6,  0.002, 0.030, "tailoff", 0.0, "grow",   "tri",   1,  "6_slow_tailoff_grow"),
        (6,  0.002, 0.030, "leadin",  0.0, "shrink", "tri",   1,  "6_slow_leadin_shrink"),
        (24, 0.0005,0.004, "both",    0.5, "const",  "sharp", 0,  "24_micro_bipolar_jit"),
    ]
    for n, w, sp, env, jit, ws, sh, pol, tag in cells:
        add(Shape("clusters", tag, "samples", cluster(n, w, sp, env, jit, ws, sh, pol),
                  n=n, atom_w=w, spacing=sp, env=env, jitter=jit, wsched=ws, shape=sh, bipolar=(pol==0)))
    # lead-in cluster INTO one big pulse, and big pulse with tail-off cluster
    lead = cluster(10, 0.0008, 0.010, "leadin", 0.2, "const", "sharp", 1, 0.6)
    big = atom(0.02, 1.0, "sharp", 2.0)
    add(Shape("clusters", "leadin_then_big", "samples", place([(0, lead[:n_of(0.10)]), (0.105, big)]), structure="lead-in -> big pulse"))
    tail = cluster(10, 0.0008, 0.010, "tailoff", 0.2, "const", "sharp", 1, 0.6)
    add(Shape("clusters", "big_then_tailoff", "samples", place([(0, big), (0.022, tail[:n_of(0.12)])]), structure="big pulse -> tail-off"))
    add(Shape("clusters", "leadin_big_tailoff", "samples",
              place([(0, lead[:n_of(0.10)]), (0.105, big), (0.127, tail[:n_of(0.12)])]), structure="lead-in -> big -> tail-off"))

# ---------------------------------------------------------------------------
# D. stochastic connect-the-dots — RedNoise-style with a ramp-length schedule
#    (native pairs, widths in SAMPLES so smoothness interpolates between dots)
# ---------------------------------------------------------------------------
def dots(total_s, w0, w1, sched="grow", boost=0.0, zct=0.0, cont=0.0, density=1.0, seed=None):
    r = np.random.default_rng(seed)
    pairs, t, last, sign = [], 0.0, 0.0, 1.0
    total = n_of(total_s)
    while t < total:
        x = t / total
        if sched == "grow":    w = w0 * (w1 / w0) ** x
        elif sched == "shrink": w = w1 * (w0 / w1) ** x
        elif sched == "hump":  w = w0 * (w1 / w0) ** math.sin(math.pi * x)
        else:                  w = w0
        w = max(1, int(round(w)))
        if r.random() < density:
            sign = -sign if r.random() < zct else (1.0 if r.random() < 0.5 else -1.0)
            v = r.uniform(boost, 1.0) * sign
            v = last + (v - last) * (1.0 - min(cont, 0.999))
        else:
            v = 0.0
        pairs.append((w, v)); last = v; t += w
    return pairs

def fam_dots():
    cells = [
        # name, total, w0, w1, sched, boost, zct, cont, density, smooth
        ("grow_white_to_pulse",   0.5, 1, 2000, "grow",   0, 0, 0, 1, 0.5),
        ("grow_white_to_pulse_s1",0.5, 1, 2000, "grow",   0, 0, 0, 1, 1.0),
        ("grow_white_to_pulse_s0",0.5, 1, 2000, "grow",   0, 0, 0, 1, 0.0),
        ("shrink_pulse_to_white", 0.5, 1, 2000, "shrink", 0, 0, 0, 1, 0.5),
        ("hump",                  0.6, 2, 1500, "hump",   0, 0, 0, 1, 0.6),
        ("grow_boost_zct",        0.4, 2, 800,  "grow",   0.7, 1.0, 0, 1, 0.5),
        ("grow_continuity",       0.4, 2, 800,  "grow",   0, 0, 0.8, 1, 0.5),
        ("grow_sparse",           0.5, 4, 1200, "grow",   0.5, 0.5, 0, 0.35, 0.5),
        ("fixed_fine_zct",        0.25, 6, 6,   "fixed",  0.3, 1.0, 0, 1, 0.0),
        ("fixed_coarse_cont",     0.5, 300, 300,"fixed",  0, 0, 0.9, 1, 1.0),
        ("shrink_sparse_zct",     0.5, 2, 1500, "shrink", 0.6, 1.0, 0, 0.5, 0.5),
        ("hump_sparse_s1",        0.6, 2, 1000, "hump",   0.4, 0.3, 0.3, 0.6, 1.0),
    ]
    for i, (nm, tot, w0, w1, sc, b, z, c, d, sm) in enumerate(cells):
        add(Shape("dots", nm, "pairs", dots(tot, w0, w1, sc, b, z, c, d, seed=100+i), sm,
                  width_unit="samples",
                  total=tot, w0=w0, w1=w1, sched=sc, boost=b, zct=z, cont=c, density=d))

# ---------------------------------------------------------------------------
# E. toy physics — bounce, SCRAPE (priority), crunch, drop-and-settle (samples)
# ---------------------------------------------------------------------------
def bounce(t_int=0.12, r_int=0.72, r_peak=0.7, atom_w=0.0015, shape="sharp", min_int=0.002, max_n=24, wshrink=False):
    ev, t, dt, pk, i = [], 0.0, t_int, 1.0, 0
    while dt > min_int and i < max_n:
        w = atom_w * (0.25 + 0.75 * (dt / t_int)) if wshrink else atom_w
        ev.append((t, atom(w, pk, shape, 2.0)))
        t += dt; dt *= r_int; pk *= r_peak; i += 1
    return place(ev)

def scrape(total_s=0.6, rate=180.0, vel="const", jitter=0.6, slip_w=0.0006, peak=0.5, press=1.0,
           grain="exp", polarity_mode="alt", seed=None, shape="saw", power=1.2):
    """Stick-slip: a train of micro slip events whose rate follows a velocity
    profile; each slip = slow build, instant release. grain = interval law."""
    r = np.random.default_rng(seed)
    ev, t, sign = [], 0.0, 1.0
    while t < total_s:
        x = t / total_s
        if vel == "const":  v = 1.0
        elif vel == "accel": v = 0.3 + 1.7 * x
        elif vel == "decel": v = 2.0 - 1.7 * x
        elif vel == "swell": v = 0.3 + 1.7 * math.sin(math.pi * x)
        else: v = 1.0
        base = 1.0 / (rate * v)
        if grain == "exp":      dt = r.exponential(base)
        elif grain == "gamma":  dt = r.gamma(4.0, base / 4.0)
        else:                   dt = base * (1 + jitter * (r.random() * 2 - 1))
        dt = max(dt, 1.0 / SR * 2)
        # heavier press: bigger, slightly longer slips; velocity brightens (shorter)
        w = slip_w * press / max(v, 0.3) ** 0.5
        pk = peak * press * (0.6 + 0.8 * r.random()) * (0.5 + 0.5 * v)
        if polarity_mode == "alt": sign = -sign
        elif polarity_mode == "rand": sign = 1.0 if r.random() < 0.5 else -1.0
        else: sign = 1.0
        ev.append((t, atom(w, pk, shape, power, 1) * sign))
        t += dt
    return place(ev)

def crunch(n_bursts=3, burst_len=0.05, gap=0.035, density=900.0, seed=None, w_lo=0.0002, w_hi=0.001):
    r = np.random.default_rng(seed)
    ev, t = [], 0.0
    for b in range(n_bursts):
        env_peak = 0.6 + 0.4 * r.random()
        tb = 0.0
        while tb < burst_len:
            w = r.uniform(w_lo, w_hi)
            pk = env_peak * r.uniform(0.2, 1.0) * math.sin(math.pi * min(tb / burst_len, 1.0)) ** 0.5
            sign = 1.0 if r.random() < 0.5 else -1.0
            ev.append((t + tb, atom(w, pk, "sharp", 1.5) * sign))
            tb += r.exponential(1.0 / density)
        t += burst_len + gap * (0.7 + 0.6 * r.random())
    return place(ev)

def fam_physics():
    add(Shape("physics", "bounce_slow", "samples", bounce(0.14, 0.78, 0.72), kind_="bounce"))
    add(Shape("physics", "bounce_fast", "samples", bounce(0.07, 0.62, 0.6), kind_="bounce"))
    add(Shape("physics", "bounce_fast_shrink", "samples", bounce(0.07, 0.62, 0.6, wshrink=True), kind_="bounce"))
    add(Shape("physics", "bounce_soft_tri", "samples", bounce(0.10, 0.7, 0.75, 0.006, "tri"), kind_="bounce"))
    # SCRAPE family (priority)
    sc = [
        ("scrape_const_light",  dict(rate=220, vel="const", peak=0.35, press=0.8, grain="exp")),
        ("scrape_const_heavy",  dict(rate=120, vel="const", peak=0.7, press=1.6, grain="exp")),
        ("scrape_accel",        dict(rate=150, vel="accel", peak=0.5, press=1.0, grain="exp")),
        ("scrape_decel",        dict(rate=150, vel="decel", peak=0.5, press=1.0, grain="exp")),
        ("scrape_swell",        dict(rate=160, vel="swell", peak=0.5, press=1.2, grain="exp", total_s=0.8)),
        ("scrape_gamma_even",   dict(rate=180, vel="const", peak=0.45, press=1.0, grain="gamma")),
        ("scrape_jitter_regular", dict(rate=90, vel="const", peak=0.5, press=1.0, grain="jit", jitter=0.25)),
        ("scrape_rand_polarity",dict(rate=180, vel="const", peak=0.45, press=1.0, grain="exp", polarity_mode="rand")),
        ("scrape_unipolar",     dict(rate=180, vel="const", peak=0.45, press=1.0, grain="exp", polarity_mode="uni")),
        ("scrape_fine_fast",    dict(rate=600, vel="const", peak=0.3, press=0.6, grain="exp", slip_w=0.0002)),
        ("scrape_coarse_slow",  dict(rate=40, vel="const", peak=0.6, press=1.5, grain="gamma", slip_w=0.002)),
        ("scrape_accel_heavy",  dict(rate=100, vel="accel", peak=0.6, press=1.8, grain="exp", slip_w=0.001, total_s=0.8)),
    ]
    for i, (nm, kw) in enumerate(sc):
        add(Shape("physics", nm, "samples", scrape(seed=500 + i, **kw), kind_="scrape", **kw))
    add(Shape("physics", "crunch_3", "samples", crunch(3, seed=900), kind_="crunch"))
    add(Shape("physics", "crunch_2_long", "samples", crunch(2, 0.09, 0.05, 700, seed=901), kind_="crunch"))
    add(Shape("physics", "crunch_5_fine", "samples", crunch(5, 0.03, 0.025, 1400, seed=902, w_hi=0.0005), kind_="crunch"))
    add(Shape("physics", "crunch_coarse", "samples", crunch(3, 0.06, 0.04, 350, seed=903, w_lo=0.0008, w_hi=0.003), kind_="crunch"))
    add(Shape("physics", "drop_settle", "samples", place([(0, bounce(0.09, 0.65, 0.6)), (0.26, crunch(2, 0.04, 0.02, 800, seed=904) * 0.5)]), kind_="drop+settle"))
    add(Shape("physics", "scrape_then_thump", "samples", place([(0, scrape(0.35, 200, "accel", peak=0.4, seed=905)), (0.36, atom(0.02, 1.0, "sharp", 2.0))]), kind_="scrape->thump"))

# ---------------------------------------------------------------------------
# F. rule-breakers
# ---------------------------------------------------------------------------
def fam_rulebreakers():
    add(Shape("rulebreak", "clip_x3_thump", "samples", place([(0, atom(0.02, 3.0, "sharp", 2.0))]), note="peak 3.0 (clips)"))
    add(Shape("rulebreak", "clip_x3_cluster", "samples", cluster(8, 0.002, 0.012, "both", 0.2, "const", "sharp", 0, 3.0), note="cluster peak 3.0"))
    z = np.tile(np.array([1.0, -1.0], dtype=np.float32), n_of(0.03) // 2) * env_curve(n_of(0.03) // 2 * 2, "tailoff").astype(np.float32)
    add(Shape("rulebreak", "alias_zigzag", "samples", place([(0, z)]), note="+-1 every sample, 30 ms"))
    add(Shape("rulebreak", "dc_ramp_drop", "pairs", [(0.2, 1.0), (0.0001, 0.0)], 0.5, note="200 ms DC ramp then drop"))
    add(Shape("rulebreak", "negative_only_cluster", "samples", cluster(8, 0.001, 0.012, "both", 0.0, "const", "sharp", -1), note="all negative"))
    add(Shape("rulebreak", "one_sample_spike", "samples", place([(0, np.array([1.0], dtype=np.float32))]), note="single sample"))
    add(Shape("rulebreak", "hold_then_snap", "pairs", [(0.0002, 1.0), (0.15, 1.0), (0.0002, 0.0)], 0.0, note="instant up, hold 150 ms, instant down"))

FAMILIES = {"atoms": fam_atoms, "twohit": fam_twohit, "clusters": fam_clusters,
            "dots": fam_dots, "physics": fam_physics, "rulebreak": fam_rulebreakers}

# ---------------------------------------------------------------------------
# Emit
# ---------------------------------------------------------------------------
def patch_json(sh, seed):
    seconds = round(sh.duration() + TAIL, 3)
    assert seconds < 10.0, f"{sh.family}/{sh.name}: {seconds}s — unit bug?"
    return {
        "sampleRate": SR, "seconds": seconds,
        "graph": {"nodes": [
            {"id": "seg", "type": "SegmentSource",
             "params": {"seed": seed, "values": sh.values(), "amplitude": 1.0,
                        "smoothness": sh.smoothness, "widthVarPct": 0.0, "valVarPct": 0.0,
                        "gap": 0.0, "gapVarPct": 0.0, "oneShot": True}},
            {"id": "ch1", "type": "SoundChannel", "inputs": {"source": "seg"},
             "params": {"volume": 0.8, "pan": 0.0}},
            {"id": "mix", "type": "StereoMixer", "inputs": {"channels": ["ch1"]},
             "params": {"gainL": 1.0, "gainR": 1.0}}],
            "output": "mix"}}

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--no-render", action="store_true")
    ap.add_argument("--family", default="")
    args = ap.parse_args()
    fams = [f for f in args.family.split(",") if f] or list(FAMILIES)
    for f in fams: FAMILIES[f]()
    manifest = []
    fails = 0
    for i, sh in enumerate(SHAPES):
        pdir = PATCH_DIR / sh.family; pdir.mkdir(parents=True, exist_ok=True)
        rdir = RENDER_DIR / sh.family; rdir.mkdir(parents=True, exist_ok=True)
        pj = patch_json(sh, 1000 + i)
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
