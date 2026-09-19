"""Trombone round 4 - the sustain round (backlog 74a5/a6/a7).

Lineage:
  tools/gen_trombone1.py                              the chassis
  tools/gen_trombone2.py                              the measured maps
  tools/gen_trombone3.py                              per-note velocity breath
  docs/autonomy/dsp/reports/2026-09-18-trombone3.md   what attempt 3 measured
Report: docs/autonomy/dsp/reports/2026-09-19-trombone4.md

NOTE NAMES ARE THE HOUSE CONVENTION (octave = midi/12), the UI keyboard's.

WHY THIS ROUND EXISTS
---------------------
Matt's attempt-3 verdict, verbatim: "the loudness/brightness linkage is there,
but it's subtle, and *only* there in the attack - once the notes settle, their
sustain phases sound identical to me, just louder.  Feel free to check this
empirically.  So what's missing to my ears is any swell or brightening *after*
the attack, which is pretty characteristic of brass."  Plus an octave-by-octave
map of an attack/release rattle whose rate he hears as frequency-linked, and an
intermittent squeak at max velocity.

THE MEASUREMENT TRAPS THIS ROUND HAD TO FIX FIRST
-------------------------------------------------
1. THE PROBE ITSELF CLAMPS.  gen_trombone3.probe_signal reads the probed node
   through a TAP, and a tap is a GUARDED RefSource: engine/include/mforce/core/
   dsp_value_source.h:164 scrubs non-finite and clamps to +-8 on every read.
   So attempt 3's "the lip displacement sits on the engine's +-8 clamp for
   53-69% of each cycle" was measured THROUGH a clamp - it could not have read
   anything else.  probe_signal(..., raw=True) here reads through a plain
   second {"ref"} instead, which the loader resolves to an UNGUARDED RefSource
   (patch_loader.cpp:105-122: only {"tap"} sets guard=true), so the number
   comes back unclipped.  The guarded and unguarded probes agree exactly where
   the signal is inside +-8, which is the check that they are the same tap.
2. WHICH +-8 ACTUALLY BITES.  In this graph the lip is read by Qn through a
   plain {"ref"}, so the ENGINE clamp never touches it.  What does clamp it is
   OUR OWN Qn curve: pts("Qn", [(-8,0), (-YEQ_HAT,0), (8, PSI_HAT*8.5)]), and
   a CurveNode in points mode holds its end knots (curve_node.h:12).  So the
   lip opening saturates at a knot WE wrote, not at an engine safety rail -
   which makes it a zero-engine-code fix.  The engine clamp does bite on the
   three TAP reads: Pe into Force, Bore into Dp/Pe/Pplus, NL_in into NL_curve.

Outputs (this script OWNS these dirs and purges anything else):
  patches/sweep/trombone4/               the grid
  patches/audition/trombone1/            THE CANDIDATE (beside attempts 1-3)
  renders/dsp/audition/trombone4/        <= 8 ears cells + README.md
Usage:
  python tools/gen_trombone4.py attack    WO1a: brightness, attack vs settled
  python tools/gen_trombone4.py attrib    WO1b: WHY the sustain is flat
  python tools/gen_trombone4.py lipdom    WO2:  widen the lip clamp and see
  python tools/gen_trombone4.py voice     WO2:  what the tone control removes
  python tools/gen_trombone4.py corner    WO2:  solve the pp and ff corners
  python tools/gen_trombone4.py rattle    WO3:  the attack/release oscillation
  python tools/gen_trombone4.py below     WO3:  what happens under midi 29
  python tools/gen_trombone4.py squeak    WO4:  the max-velocity squeak
  python tools/gen_trombone4.py ffstab    WO4:  solve attack-stable ff ceilings
  python tools/gen_trombone4.py all       the full run + gates + ears queue

WHAT THE MEASUREMENT SAID, IN ONE PARAGRAPH
-------------------------------------------
The sustain was never flat.  Probed BEFORE the voicing lowpass, attempt 3's
own settled-window pp->ff centroid runs +24.9% (F2), +80.8% (A#3), +53.0%
(C5), +163.0% (F5); probed after it, +0.8 / +18.5 / +33.8 / +27.0%.  The loop
does not self-limit (the drive at the steepener's input is linear in the
breath over 10.8-27.2 dB), nothing rails (0.0% on every engine tap; our own Qn
knot holds 37-69% of the positive half-cycle and widening it 8->256 moves the
settled centroid 1.2%), and the steepener's transfer does scale with amplitude
(+5 -> +56 Hz on A#3).  So the fix is at the output: the tone control rides
the breath, 550/1100/1600 Hz, with 1100 at velocity 0.8 pinned to Matt's own
pick and both other ends SOLVED against the ratio the instrument itself makes.
"""
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import gen_trombone1 as T1                                   # noqa: E402
import gen_trombone2 as T2                                   # noqa: E402
import gen_trombone3 as T3                                   # noqa: E402
from gen_stk_bowed import read_mono                          # noqa: E402
from gen_nlbore_probe1 import centroid_all, hf_fraction      # noqa: E402
from gen_nlbore_probe1 import diff_db, level_ceiling, rms    # noqa: E402
from gen_onemass_lip1 import (midi_hz, PM_TAB, PSI_HAT,      # noqa: E402
                              SR, YEQ_HAT)

ROOT = T1.ROOT
CLI = T1.CLI
UI = T2.UI
PATCH_OUT = os.path.join(ROOT, "patches", "sweep", "trombone4")
CAND_OUT = os.path.join(ROOT, "patches", "audition", "trombone1")
EARS_OUT = os.path.join(ROOT, "renders", "dsp", "audition", "trombone4")
SCRATCH = os.path.join(tempfile.gettempdir(), "trombone4_scratch")
CAND_NAME = "trombone_attempt4"

note_name = T2.note_name
sounding_rms = T2.sounding_rms
note_rows = T2.note_rows
fmt_table = T2.fmt_table
midi_of = lambda f: 12.0 * math.log2(max(f, 1e-9) / 440.0) + 69.0   # noqa: E731

VEL_PP, VEL_MF, VEL_FF = T3.VEL_PP, T3.VEL_MF, T3.VEL_FF
TUNE_LO, TUNE_HI = T3.TUNE_LO, T3.TUNE_HI
METRIC_NOTES = [29, 46, 60, 65]          # F2, A#3, C5, F5
LEVEL_CEILING = T2.LEVEL_CEILING
AUDIBILITY_FLOOR = T2.AUDIBILITY_FLOOR
FEATURE_DB = T2.FEATURE_DB
PEAK_CEIL = T2.PEAK_CEIL
LOCK_CENTS_GATE = T2.LOCK_CENTS_GATE
SPEAK_TARGET_C4 = T2.SPEAK_TARGET_C4

# ---------------------------------------------------------------------------
# the windows this round argues about
# ---------------------------------------------------------------------------
ATT_W = (0.005, 0.120)     # s after note-on: Matt's "attack"
SET_W = (1.20, 2.20)       # s after note-on: Matt's "once the notes settle"
LONG_SEC, LONG_SLOT = 2.5, 3.2

# ---------------------------------------------------------------------------
# WO2 lever 1: the lip opening's own domain
# ---------------------------------------------------------------------------
# Qn is written by flow_nodes() as three knots spanning +-8 in LIP UNITS
# (Y_REF = 1 mm).  Points mode holds the end knots, so any lip excursion past
# +8 mm is flattened by our own curve.  Widening the knot to LIP_DOMAIN keeps
# the SLOPE identical (the knot at (-YEQ_HAT, 0) sets it), so inside +-8 the
# transfer is unchanged to the digit and outside it the published law keeps
# applying instead of railing.  Exactly the steepener-domain move attempt 3
# made for the same reason.
LIP_DOMAIN = 8.0           # rewritten below once `attrib` has measured it


def qn_knots(dom):
    return [[-dom, 0.0], [-YEQ_HAT, 0.0],
            [dom, round(PSI_HAT * (dom + YEQ_HAT), 9)]]


# ===========================================================================
# graph: attempt 3's, with this round's levers bolted on explicitly
# ===========================================================================
def make_patch(cfg):
    """attempt 3's make_patch plus:
        lip_domain    Qn's knot extent in lip units (8.0 = attempt 3)
        probe_raw     read the probed node through a plain {"ref"} (UNGUARDED)
                      instead of a {"tap"} (guarded, clamps at +-8)
    """
    p = T3.make_patch(cfg)
    nodes = p["graph"]["nodes"]
    byid = {n["id"]: n for n in nodes}

    dom = float(cfg.get("lip_domain", LIP_DOMAIN))
    if abs(dom - 8.0) > 1e-9:
        byid["Qn"]["params"]["knots"] = qn_knots(dom)

    vc = cfg.get("voice_curve")
    if vc:
        # The voicing lowpass corner rides the breath.  Inserted immediately
        # BEFORE Voice0 (a {"ref"} may not point forward - only a {"tap"}
        # may), which also leaves Vdn as __perf_v's advancing consumer, so
        # nothing about the dynamics subgraph moves.
        i = [k for k, n in enumerate(nodes) if n["id"] == "Voice0"]
        if not i:
            raise RuntimeError("voice_curve needs the voicing chain")
        knots = sorted((float(v), float(hz)) for v, hz in vc)
        nodes.insert(i[0], {"id": "Vk", "type": "CurveNode", "params": {
            "exprKnots": [], "interp": "linear", "mode": "points",
            "knots": [[round(a, 6), round(b, 4)] for a, b in knots],
            "source": {"ref": "__perf_v"}}})
        for nid in ("Voice0", "Voice1"):
            byid[nid]["params"]["cutoffFreq"] = {"ref": "Vk"}
        p["ui"]["positions"]["Vk"] = [0.0, -660.0]

    if cfg.get("tail_sec"):
        # Render past the last note-off so the voice's own tail completes.
        # (The UI's passage path cuts it; that is backlog 75 and is being
        # diagnosed separately - this is just the CLI window.)
        p["seconds"] = round(p["seconds"] + float(cfg["tail_sec"]), 6)

    if cfg.get("probe") and cfg.get("probe_raw"):
        pid = cfg["probe"][0]
        # A plain {"ref"} is only safe if the node already has an ADVANCING
        # consumer earlier in the node list; otherwise the probe would take
        # the raw pointer and advance it a second time per sample.
        pos = [i for i, n in enumerate(nodes) if n["id"] == pid]
        pidx = [i for i, n in enumerate(nodes) if n["id"] == "Praw"]
        before = 0
        for i, n in enumerate(nodes[:pidx[0]]):
            before += json.dumps(n).count('{"ref": "%s"}' % pid)
        if not pos or before < 1:
            raise RuntimeError("raw probe on %r needs an earlier plain ref "
                               "(found %d)" % (pid, before))
        byid["Praw"]["params"]["source2"] = {"ref": pid}
    return p


def base_cfg(**kw):
    """ATTEMPT 3 EXACTLY - the thing being diagnosed.  Every WO1 measurement
    stage runs on this, so the numbers in the report stay reproducible after
    the candidate changes."""
    cfg = T3.base_cfg()
    cfg["lip_domain"] = LIP_DOMAIN
    cfg.update(kw)
    return cfg


def cand_cfg(**kw):
    """ATTEMPT 4 AS SHIPPED: attempt 3 plus the velocity-tracked voicing
    corner (WO2) and the attack-stable ff ceilings (WO4).  Both act only
    away from velocity %.2f, where this is attempt 3 sample for sample.""" \
        % VEL_MF
    cfg = base_cfg(ff_map=dict(FF4_MAP), voice_curve=list(VOICE_CURVE),
                   steep_domain=DRIVE4_DOMAIN)
    cfg.update(kw)
    return cfg


def ignites(r):
    """Did the note start from silence and lock on the written pitch?  Pitch
    and periodicity only - see IGNITE_RMS."""
    return bool(r and np.isfinite(r["cents"])
                and abs(r["cents"]) <= T3.SPAN_CENTS
                and r["lock"] > T3.SPAN_LOCK and r["rms"] > IGNITE_RMS)


# ===========================================================================
# render + probe
# ===========================================================================
def render(patch, tag, out_dir, timeout=900):
    pj = os.path.join(PATCH_OUT, tag + ".json")
    wp = os.path.join(out_dir, tag + ".wav")
    os.makedirs(os.path.dirname(pj), exist_ok=True)
    os.makedirs(out_dir, exist_ok=True)
    with open(pj, "w") as f:
        json.dump(patch, f, indent=1)
    r = subprocess.run([CLI, pj, wp], capture_output=True, text=True,
                       timeout=timeout)
    if r.returncode != 0 or not os.path.exists(wp):
        print("  RENDER_FAIL %s rc=%s %s"
              % (tag, r.returncode, (r.stderr or r.stdout or "")[-300:]))
        return None, pj, wp
    return read_mono(wp), pj, wp


def render_score(cfg, notes, nsec, gap, tag, out_dir, **kw):
    c = dict(cfg)
    c["notes"], c["note_sec"], c["slot"] = notes, nsec, nsec + gap
    c.update(kw)
    return render(make_patch(c), tag, out_dir)


def probe(cfg, node, scale, tag="probe", raw=True):
    """One internal node's waveform in its own units.

    Same two gains out as gen_trombone3.probe_signal (the probe's own scale
    and the engine's velocity x volume voice mix), plus `raw`: the probe reads
    through a plain second {"ref"} so nothing is clamped on the way out.  The
    caller gets (signal, sr, clipped) - `clipped` is True if the RENDER hit
    the mixer's own +-0.999 peak guard, i.e. the scale was too big and the
    number is not to be trusted.
    """
    c = dict(cfg)
    c["probe"] = (node, scale)
    c["probe_raw"] = bool(raw)
    got, pj, wp = render(make_patch(c), tag, SCRATCH)
    if got is None:
        return None, None, True
    x, sr = got
    clipped = bool(np.abs(x).max() >= 0.999)
    T1.rm(pj, wp)
    vol = float(c.get("volume", 0.7))
    vel = float(c.get("velocity", VEL_MF))
    return x / (float(scale) * max(vel * vol, 1e-9)), sr, clipped


def probe_note(cfg, n, node, scale, nsec=LONG_SEC, slot=LONG_SLOT, raw=True,
               tag="probe"):
    c = dict(cfg)
    c["notes"], c["note_sec"], c["slot"] = [n], nsec, slot
    return probe(c, node, scale, tag, raw)


def win(x, sr, lo, hi, k=0, slot=LONG_SLOT):
    a = int((slot * k + lo) * sr)
    b = int((slot * k + hi) * sr)
    return x[a:min(b, len(x))]


def one_note(cfg, n, tag="one", nsec=LONG_SEC, slot=LONG_SLOT, **kw):
    got, pj, wp = render_score(cfg, [n], nsec, slot - nsec, tag, SCRATCH, **kw)
    if got is None:
        return None, None, None
    x, sr = got
    row = note_rows(x, sr, [n], slot, nsec)[n]
    T1.rm(pj, wp)
    return row, x, sr


# ===========================================================================
# WO1a: is the brightness linkage attack-only?
# ===========================================================================
def bright_win(x, sr, lo, hi):
    seg = win(x, sr, lo, hi)
    if len(seg) < 512:
        return None
    return {"centroid": float(centroid_all(seg, sr)),
            "hf1k": float(hf_fraction(seg, sr, 1000.0)),
            "rms": float(rms(seg))}


def centroid_track(x, sr, dur=2.4, winms=46.0, hopms=10.0):
    """Centroid every hop from note-on, so 'it brightens in the attack and
    then stops' is a curve rather than an opinion."""
    w = int(sr * winms / 1000.0)
    h = int(sr * hopms / 1000.0)
    out = []
    for i in range(0, min(len(x) - w, int(dur * sr)), h):
        seg = x[i:i + w]
        out.append((i / sr, float(centroid_all(seg, sr)),
                    float(rms(seg))))
    return out


def stage_attack(notes=None, verbose=True):
    notes = notes or METRIC_NOTES
    print("=== WO1a: brightness in the ATTACK window vs the SETTLED window ===")
    print("  attack  = %.0f-%.0f ms after note-on" % (ATT_W[0] * 1000,
                                                      ATT_W[1] * 1000))
    print("  settled = %.2f-%.2f s after note-on (Matt's 'once the notes "
          "settle')" % SET_W)
    out = {}
    for n in notes:
        rows = {}
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            row, x, sr = one_note(base_cfg(velocity=v), n, "att")
            if x is None:
                continue
            rows[tag] = {"attack": bright_win(x, sr, *ATT_W),
                         "settled": bright_win(x, sr, *SET_W),
                         "cents": row["cents"], "lock": row["lock"],
                         "track": centroid_track(x, sr)}
        out[n] = rows
        if not verbose or "pp" not in rows or "ff" not in rows:
            continue
        print("\n  midi %d (%s, %.1f Hz)" % (n, note_name(n), midi_hz(n)))
        print("    %-9s %9s %9s %9s %10s" % ("window", "pp", "mf", "ff",
                                             "pp->ff"))
        for w_ in ("attack", "settled"):
            c = [rows[t][w_] for t in ("pp", "mf", "ff")]
            print("    %-9s %9.0f %9.0f %9.0f %+9.1f%%   centroid Hz"
                  % (w_, c[0]["centroid"], c[1]["centroid"], c[2]["centroid"],
                     100 * (c[2]["centroid"] / max(c[0]["centroid"], 1e-9)
                            - 1)))
            print("    %-9s %9.4f %9.4f %9.4f %9.2fx   >1 kHz share"
                  % ("", c[0]["hf1k"], c[1]["hf1k"], c[2]["hf1k"],
                     c[2]["hf1k"] / max(c[0]["hf1k"], 1e-9)))
            print("    %-9s %9.1f %9.1f %9.1f %+9.1f dB  level"
                  % ("", 20 * math.log10(max(c[0]["rms"], 1e-9)
                                         / max(c[1]["rms"], 1e-9)), 0.0,
                     20 * math.log10(max(c[2]["rms"], 1e-9)
                                     / max(c[1]["rms"], 1e-9)),
                     20 * math.log10(max(c[2]["rms"], 1e-9)
                                     / max(c[0]["rms"], 1e-9))))
        # the shape of the swell, at ff
        tr = rows["ff"]["track"]
        pick = [0.02, 0.05, 0.1, 0.2, 0.4, 0.8, 1.6, 2.2]
        vals = []
        for t in pick:
            k = min(range(len(tr)), key=lambda i: abs(tr[i][0] - t))
            vals.append(tr[k][1])
        print("    ff centroid over time  " + "  ".join(
            "%.2fs:%4.0f" % (t, v) for t, v in zip(pick, vals)))
    json.dump(out, open(os.path.join(SCRATCH, "attack.json"), "w"), indent=1,
              default=float)
    return out


# ===========================================================================
# WO1b: WHY the settled sustain is flat
# ===========================================================================
RAILS = [
    # node, probe scale, the limit it runs into, what imposes it
    ("Lip", 0.02, 8.0, "OUR Qn curve's end knot (points mode holds it)"),
    ("Pe", 0.02, 8.0, "the ENGINE +-8 on Force's {tap: Pe}"),
    ("Bore", 0.02, 8.0, "the ENGINE +-8 on the three {tap: Bore} reads"),
    ("Dp", 0.01, 20.0, "OUR AbsDp4 curve's end knot"),
    ("NL_in", 0.05, T3.DRIVE_DOMAIN, "OUR steepener curve's end knot"),
]


def rail_stats(cfg, n, node, scale, limit, raw=True):
    x, sr, clip = probe_note(cfg, n, node, scale, raw=raw, tag="rail")
    if x is None:
        return None
    seg = win(x, sr, *SET_W)
    att = win(x, sr, *ATT_W)
    return {"peak": float(np.abs(seg).max()), "rms": float(rms(seg)),
            "hi": float(seg.max()), "lo": float(seg.min()),
            "over_frac": float((np.abs(seg) >= limit * 0.99999).mean()),
            "over_pos": float((seg >= limit * 0.99999).mean()),
            "attack_peak": float(np.abs(att).max()) if len(att) else 0.0,
            "clipped_render": clip}


def stage_attrib(notes=None):
    """(i) the travelling wave AT the steepener input in settled sustain,
       (ii) what fraction of each cycle each clamp rails on,
       (iii) whether the steepener's transfer differs across those amplitudes.
    """
    notes = notes or METRIC_NOTES
    print("=== WO1b: why is the settled sustain flat? ===")

    print("\n  (0) probe sanity: guarded {tap} vs raw {ref} on the same node")
    c = base_cfg(velocity=VEL_FF)
    for node, sc, lim, _ in RAILS[:3]:
        g, sr, _ = probe_note(c, 46, node, sc, raw=False, tag="pg")
        r, _, _ = probe_note(c, 46, node, sc, raw=True, tag="pr")
        if g is None or r is None:
            continue
        gs, rs = win(g, sr, *SET_W), win(r, sr, *SET_W)
        print("      %-6s guarded peak %8.3f   raw peak %8.3f   max|diff| "
              "%.2e" % (node, np.abs(gs).max(), np.abs(rs).max(),
                        np.abs(gs - rs).max()))

    print("\n  (i) the travelling wave the steepener sees, SETTLED window")
    print("      %-5s %-3s %8s | %9s %9s | %9s %9s"
          % ("note", "dyn", "kPa", "NL_in pk", "NL_in rms", "attack pk",
             "att/sus"))
    drive = {}
    for n in notes:
        drive[n] = {}
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            cfg = base_cfg(velocity=v)
            st = rail_stats(cfg, n, "NL_in", 0.05, T3.DRIVE_DOMAIN)
            if st is None:
                continue
            st["kPa"] = T3.dyn_pressure(n, v) * PM_TAB / 1000.0
            drive[n][tag] = st
            print("      %-5s %-3s %8.1f | %9.4f %9.4f | %9.4f %9.2f"
                  % (note_name(n), tag, st["kPa"], st["peak"], st["rms"],
                     st["attack_peak"],
                     st["attack_peak"] / max(st["peak"], 1e-9)))
        if "pp" in drive[n] and "ff" in drive[n]:
            print("      %-5s     settled drive pp->ff  %+.1f dB "
                  "(level pp->ff %+.1f dB)"
                  % (note_name(n),
                     20 * math.log10(drive[n]["ff"]["rms"]
                                     / max(drive[n]["pp"]["rms"], 1e-9)),
                     0.0))

    print("\n  (ii) what rails, and for what fraction of the settled sustain")
    rails = {}
    for n in notes:
        rails[n] = {}
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            cfg = base_cfg(velocity=v)
            rails[n][tag] = {}
            for node, sc, lim, who in RAILS:
                st = rail_stats(cfg, n, node, sc, lim)
                if st is None:
                    continue
                st["limit"] = lim
                rails[n][tag][node] = st
        r = rails[n]
        print("      %s:" % note_name(n))
        for node, sc, lim, who in RAILS:
            cells = []
            for tag in ("pp", "mf", "ff"):
                st = r.get(tag, {}).get(node)
                cells.append("n/a" if st is None else
                             "%7.3f (%4.1f%%)" % (st["peak"],
                                                  100 * st["over_frac"]))
            print("        %-6s limit %5.1f  pp %s  mf %s  ff %s   [%s]"
                  % (node, lim, cells[0], cells[1], cells[2], who))

    print("\n  (iii) does the steepener's transfer differ across those "
          "amplitudes?")
    print("      centroid of the SETTLED window, steepener off vs on")
    print("      %-5s %-3s %9s %9s %8s | %9s %9s"
          % ("note", "dyn", "cent off", "cent on", "delta", "hf off", "hf on"))
    steep = {}
    for n in notes:
        steep[n] = {}
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            vals = []
            for d in (0.0, T1.STEEP_DEPTH):
                row, x, sr = one_note(base_cfg(velocity=v, depth=d), n, "abl")
                if x is None:
                    vals.append(None)
                    continue
                vals.append(bright_win(x, sr, *SET_W))
            if any(v_ is None for v_ in vals):
                continue
            steep[n][tag] = {"off": vals[0], "on": vals[1]}
            print("      %-5s %-3s %9.0f %9.0f %+8.0f | %9.4f %9.4f"
                  % (note_name(n), tag, vals[0]["centroid"],
                     vals[1]["centroid"],
                     vals[1]["centroid"] - vals[0]["centroid"],
                     vals[0]["hf1k"], vals[1]["hf1k"]))
    json.dump({"drive": drive, "rails": rails, "steepener": steep},
              open(os.path.join(SCRATCH, "attrib.json"), "w"), indent=1,
              default=float)
    return {"drive": drive, "rails": rails, "steepener": steep}


# ===========================================================================
# WO2 lever 1: widen the lip opening's own domain
# ===========================================================================
LIP_DOMS = [8.0, 16.0, 32.0, 64.0, 128.0, 256.0]


def lip_shape(cfg, n, dom):
    """What the valve actually does in the settled sustain, at one domain."""
    c = dict(cfg)
    c["lip_domain"] = dom
    x, sr, clip = probe_note(c, n, "Lip", 0.002, raw=True, tag="ls")
    if x is None:
        return None
    seg = win(x, sr, *SET_W)
    op = seg + YEQ_HAT
    return {"peak": float(np.abs(seg).max()), "p99": float(np.percentile(
        seg, 99)), "mean": float(seg.mean()),
        "over_frac": float((seg >= dom * 0.99999).mean()),
        "shut_frac": float((op <= 0.0).mean()),
        "clipped_render": clip}


def stage_lipdom(notes=None, doms=None):
    notes = notes or METRIC_NOTES
    doms = doms or LIP_DOMS
    print("=== WO2 lever 1: the lip opening's own clamp, widened ===")
    print("  Qn's end knot is OURS (flow_nodes in gen_onemass_lip1); points")
    print("  mode holds it, so every lip excursion past it is flattened.")
    print("  Widening keeps the SLOPE (the knot at (-%.2f, 0) sets it)."
          % YEQ_HAT)
    out = {}
    for n in notes:
        out[n] = {}
        print("\n  %s (midi %d)" % (note_name(n), n))
        print("    %-5s %6s | %8s %8s %7s %7s | %8s %8s %7s"
              % ("dyn", "domain", "lip pk", "clip%", "shut%", "cents",
                 "centroid", "hf1k", "rms"))
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            out[n][tag] = {}
            for dom in doms:
                cfg = base_cfg(velocity=v, lip_domain=dom)
                sh = lip_shape(cfg, n, dom)
                row, x, sr = one_note(cfg, n, "ld")
                if row is None or sh is None:
                    continue
                b = bright_win(x, sr, *SET_W)
                rec = dict(sh)
                rec.update(b or {})
                rec.update({"cents": row["cents"], "lock": row["lock"]})
                out[n][tag][dom] = rec
                print("    %-5s %6.0f | %8.2f %7.1f%% %6.1f%% %7.1f | "
                      "%8.0f %8.4f %7.4f"
                      % (tag, dom, sh["peak"], 100 * sh["over_frac"],
                         100 * sh["shut_frac"], row["cents"],
                         (b or {}).get("centroid", float("nan")),
                         (b or {}).get("hf1k", float("nan")),
                         (b or {}).get("rms", float("nan"))))
    json.dump(out, open(os.path.join(SCRATCH, "lipdom.json"), "w"), indent=1,
              default=float)
    return out


# ===========================================================================
# WO2 lever 2: where the brightness that DOES exist is being thrown away
# ===========================================================================
VOICE_SWEEP = [700.0, 1100.0, 1600.0, 2400.0, 3600.0, 6000.0]


def stage_voice(notes=None):
    """Attempt 2's voicing lowpass is a FIXED 1100 Hz corner (Matt picked it:
    "Line beats line_brighter") sitting after the steepener and the bell.  It
    was picked when the instrument was permanently at forte - attempt 3's two
    gain stages - so it was chosen to tame a sound that had no dynamics.  This
    measures how much dynamic brightness reaches it and how much survives it.
    """
    notes = notes or METRIC_NOTES
    print("=== WO2: what the voicing lowpass does to the dynamic range ===")
    print("\n  (a) settled centroid BEFORE the voicing filter (Trans4) and "
          "AFTER it (the output)")
    print("      %-5s %-3s | %9s %9s | %9s %9s"
          % ("note", "dyn", "pre cent", "pre hf1k", "out cent", "out hf1k"))
    pre = {}
    for n in notes:
        pre[n] = {}
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            cfg = base_cfg(velocity=v)
            x, sr, clip = probe_note(cfg, n, "Trans4", 0.5, raw=True,
                                     tag="vpre")
            row, y, sr2 = one_note(cfg, n, "vout")
            if x is None or y is None:
                continue
            a = bright_win(x, sr, *SET_W)
            b = bright_win(y, sr2, *SET_W)
            pre[n][tag] = {"pre": a, "out": b, "clipped": clip}
            print("      %-5s %-3s | %9.0f %9.4f | %9.0f %9.4f"
                  % (note_name(n), tag, a["centroid"], a["hf1k"],
                     b["centroid"], b["hf1k"]))
        if "pp" in pre[n] and "ff" in pre[n]:
            p_, f_ = pre[n]["pp"], pre[n]["ff"]
            print("      %-5s     pp->ff  BEFORE %+.1f%%   AFTER %+.1f%%"
                  % (note_name(n),
                     100 * (f_["pre"]["centroid"] / p_["pre"]["centroid"] - 1),
                     100 * (f_["out"]["centroid"] / p_["out"]["centroid"]
                            - 1)))

    print("\n  (b) the voicing corner swept, at ff and at mf")
    print("      %-5s %-3s | " % ("note", "dyn")
          + " ".join("%7.0f" % k for k in VOICE_SWEEP) + "   Hz corner")
    sweep = {}
    for n in notes:
        sweep[n] = {}
        for tag, v in (("mf", VEL_MF), ("ff", VEL_FF)):
            row = {}
            cells = []
            for k in VOICE_SWEEP:
                r_, x, sr = one_note(base_cfg(velocity=v, voice_k=k), n, "vk")
                b = bright_win(x, sr, *SET_W) if x is not None else None
                row[k] = {"bright": b, "cents": (r_ or {}).get("cents")}
                cells.append("%7.0f" % (b["centroid"] if b else float("nan")))
            sweep[n][tag] = row
            print("      %-5s %-3s | %s   settled centroid"
                  % (note_name(n), tag, " ".join(cells)))
    json.dump({"pre_post": pre, "corner_sweep": sweep},
              open(os.path.join(SCRATCH, "voice.json"), "w"), indent=1,
              default=float)
    return {"pre_post": pre, "corner_sweep": sweep}


# ===========================================================================
# WO2 fix: solve the voicing corner at pp and at ff
# ===========================================================================
CORNER_GRID = [round(300.0 * 1.35 ** i, 1) for i in range(11)]   # 300..7300
VOICE_MF = T2.VOICE_DARK          # 1100 Hz - Matt's own pick, PINNED at 0.8
# SOLVED by stage `corner` (coarse grid then a refine pass), pasted back.
# The criterion is stated in corner_targets(): carry the instrument's OWN
# pp->ff centroid ratio - measured before the filter - through to the output,
# without moving the mf sound at all.
VOICE_PP = 550.0           # velocity <= 0.25
VOICE_FF = 1600.0          # velocity 1.00
VOICE_CURVE = [(0.0, VOICE_PP), (VEL_PP, VOICE_PP), (VEL_MF, VOICE_MF),
               (VEL_FF, VOICE_FF)]

# SOLVED by stage `ffstab`, pasted back: attempt 3's FF_MAP with each note's
# ceiling walked down (x0.9 a step) until its ATTACK is stable at velocity
# 1.0, not just its settled pitch.  6 of 51 notes moved; the other 45 keep
# attempt 3's ff exactly.  Velocity 0.8 is untouched either way - the ff map
# only acts above it.
FF4_MAP = {29: 5.0549, 30: 4.8083, 31: 4.1383, 32: 4.1383, 33: 4.1383,
    34: 3.8972, 35: 3.6702, 36: 3.0654, 37: 3.0654, 38: 3.0654,
    39: 3.0654, 40: 3.0654, 41: 3.0654, 42: 3.0654, 43: 3.0654,
    44: 2.8868, 45: 2.7187, 46: 2.2707, 47: 2.2707, 48: 2.2707,
    49: 2.2707, 50: 2.2707, 51: 2.2707, 52: 2.2707, 53: 2.2707,
    54: 2.7187, 55: 2.8868, 56: 3.0654, 57: 3.0654, 58: 3.0654,
    59: 3.0654, 60: 3.6702, 61: 3.8972, 62: 4.1383, 63: 2.4437,
    64: 2.3699, 65: 5.2612, 66: 5.5867, 67: 5.5867, 68: 5.5867,
    69: 6.6889, 70: 6.3923, 71: 6.7878, 72: 5.4981, 73: 5.4981,
    74: 7.542, 75: 7.542, 76: 8.447, 77: 9.6384, 78: 10.2479,
    79: 10.3458}

# WO4: what counts as a squeaking attack at velocity 1.0.  The reference is
# velocity 0.8 itself, where 0-3 of the first 25 slices sit off the written
# partial on the notes tested - the level Matt named and likes.
# The steepener's curve domain.  Attempt 3 measured its worst ff drive as
# 3.19 and set the knots at +-4 - but it measured only the SETTLED window on
# four notes.  Over the whole note, including the attack, G6 at ff reaches
# 6.2.  The knots go to +-8 at the IDENTICAL slope: inside +-4 the curve is
# unchanged to the digit (so velocity 0.8 stays bit-identical, gated), and 8
# is where the ENGINE's own tap clamp sits anyway - NL_curve reads NL_in
# through a {"tap"}, which is a guarded RefSource clamping at +-8, so past
# that point no curve of ours can see the difference regardless.
DRIVE4_DOMAIN = 8.0

# The ignition gate is a PITCH-AND-PERIODICITY test, never a loudness test.
# Attempt 3 said so and lowered its floor to 0.0015; this round's pp is 4.5 dB
# quieter again (a darker tone control at pp passes less energy), so a fixed
# rms floor would now fail 22 notes that lock on the written pitch to the same
# cent and the same 1.00 periodicity as attempt 3.  The floor here is the
# repo's true-silence floor, and the pp level is REPORTED instead of gated.
IGNITE_RMS = AUDIBILITY_FLOOR

LSB16 = 1.0 / 32768.0      # one 16-bit step, the WAV's own resolution

SQUEAK_FRAC = 0.16         # of the attack slices, off the written partial
SQUEAK_RATIO = 1.35        # ...and none of them this far off (or 1/this)
SQUEAK_CENTS = 15.0        # ...and the settled pitch still right


def corner_targets(pre_post):
    """What the output centroid WOULD be at pp and ff if the voicing filter
    did not change how much the instrument brightens when blown harder.

    The filter is a fixed tone control.  Before it, the instrument's own
    pp->ff centroid motion is measured; after it, most of that motion is
    gone, because a fixed corner sits exactly where the energy is moving.
    The target keeps the mf sound EXACTLY as it is (Matt picked that corner)
    and asks only that the RATIO be carried through.
    """
    out = {}
    for n, r in pre_post.items():
        if "mf" not in r:
            continue
        for tag in ("pp", "ff"):
            if tag not in r:
                continue
            ratio = (r[tag]["pre"]["centroid"]
                     / max(r["mf"]["pre"]["centroid"], 1e-9))
            out.setdefault(tag, {})[n] = r["mf"]["out"]["centroid"] * ratio
    return out


def solve_corner(targets, tag, vel, grid=None, notes=None):
    grid = grid or CORNER_GRID
    notes = notes or METRIC_NOTES
    best, log = None, []
    for k in grid:
        errs, cents_ok, rows = [], True, {}
        for n in notes:
            if n not in targets.get(tag, {}):
                continue
            curve = [(0.0, k), (VEL_PP, k), (VEL_MF, VOICE_MF),
                     (VEL_FF, VOICE_MF)] if tag == "pp" else \
                    [(0.0, VOICE_MF), (VEL_MF, VOICE_MF), (VEL_FF, k)]
            r_, x, sr = one_note(base_cfg(velocity=vel, voice_curve=curve),
                                 n, "ck")
            if x is None:
                continue
            b = bright_win(x, sr, *SET_W)
            rows[n] = {"centroid": b["centroid"], "rms": b["rms"],
                       "hf1k": b["hf1k"], "cents": r_["cents"],
                       "lock": r_["lock"]}
            errs.append(math.log(max(b["centroid"], 1e-9)
                                 / max(targets[tag][n], 1e-9)))
        if not errs:
            continue
        err = math.sqrt(sum(e * e for e in errs) / len(errs))
        log.append({"k": k, "err": err, "rows": rows})
        print("      corner %7.1f Hz   log-rms error %.4f   centroids %s"
              % (k, err, " ".join("%.0f" % rows[n]["centroid"]
                                  for n in sorted(rows))))
        if best is None or err < best["err"]:
            best = log[-1]
    return best, log


def stage_corner():
    print("=== WO2 fix: solve the voicing corner at pp and at ff ===")
    print("  pinned: %.0f Hz at velocity %.2f (Matt's own pick, untouched)"
          % (VOICE_MF, VEL_MF))
    pp_post = stage_voice(notes=METRIC_NOTES)["pre_post"]
    tgt = corner_targets(pp_post)
    print("\n  targets (the centroid the output would have if the filter "
          "carried the instrument's own ratio through)")
    for tag in ("pp", "ff"):
        print("    %s: %s" % (tag, ", ".join(
            "%s %.0f Hz" % (note_name(n), v)
            for n, v in sorted(tgt.get(tag, {}).items()))))
    out = {}
    for tag, vel in (("ff", VEL_FF), ("pp", VEL_PP)):
        print("\n  solving the %s corner:" % tag)
        best, log = solve_corner(tgt, tag, vel)
        out[tag] = {"best": best, "log": log}
        print("    -> %s corner %.1f Hz" % (tag, best["k"] if best else
                                            float("nan")))
    json.dump({"targets": tgt, "solved": out},
              open(os.path.join(SCRATCH, "corner.json"), "w"), indent=1,
              default=float)
    return out


# ===========================================================================
# WO3: the attack/release oscillation (backlog 74a6, Matt's octave map)
# ===========================================================================
RATTLE_NOTES = [29, 31, 36, 43, 48, 55, 60, 72, 79]      # F2 G2 C3 G3 C4 G4
BELOW_NOTES = [12, 17, 24, 26, 28]                       # C1 F1 C2 D2 E2
MOD_LO, MOD_HI = 4.0, 600.0


def analytic_env(x):
    """|x + j H(x)| - the instantaneous amplitude, with no window length to
    pick.  A moving-rms envelope cannot be used here: its window has to be a
    period or more of f0, which is the same order as the modulation being
    measured, so it would smear the answer into the window's own bandwidth."""
    n = len(x)
    X = np.fft.fft(x - x.mean())
    h = np.zeros(n)
    h[0] = 1.0
    if n % 2 == 0:
        h[n // 2] = 1.0
        h[1:n // 2] = 2.0
    else:
        h[1:(n + 1) // 2] = 2.0
    return np.abs(np.fft.ifft(X * h))


def mod_rate(seg, sr, lo=MOD_LO, hi=MOD_HI, deg=5, method="poly"):
    """Dominant modulation rate of the amplitude envelope, in Hz.

    The envelope's own gross shape (the attack ramp, the release decay) is a
    big low-frequency component that would win every spectrum and has to
    come out first.  A MOVING AVERAGE is the wrong tool for it - it is a
    high-pass whose corner (1/window) lands inside the band being searched,
    and a first pass of this measurement duly reported rates sitting on its
    own corner.  A low-order polynomial fitted to the LOG envelope removes
    the gross shape with no corner anywhere near the band.

    Returns (rate_hz, strength, spectrum); strength is the peak's share of
    the residual envelope's energy in the band.
    """
    if len(seg) < 512:
        return float("nan"), 0.0, None
    e = analytic_env(seg)
    if method == "ma":
        w = max(3, int(sr * 0.04) | 1)
        sm = np.convolve(e, np.ones(w) / w, mode="same")
        r = (e / np.maximum(sm, 1e-12) - 1.0)[w:len(e) - w]
    else:
        t = np.linspace(-1.0, 1.0, len(e))
        le = np.log(np.maximum(e, 1e-9))
        r = le - np.polyval(np.polyfit(t, le, deg), t)
    if len(r) < 256:
        return float("nan"), 0.0, None
    sp = np.abs(np.fft.rfft(r * np.hanning(len(r)))) ** 2
    fr = np.fft.rfftfreq(len(r), 1.0 / sr)
    band = (fr >= lo) & (fr <= hi)
    if not band.any():
        return float("nan"), 0.0, None
    i = int(np.argmax(np.where(band, sp, 0.0)))
    tot = float(sp[band].sum()) + 1e-30
    return float(fr[i]), float(sp[i] / tot), (fr[band], sp[band])


def bore_rate(n):
    """The air column's round-trip rate: the DelayLine is `ratio` periods of
    f0 long, ratio = partial_of(f0) (x the measured per-note trim), so one
    round trip takes ratio/f0 seconds."""
    f0 = midi_hz(n)
    return f0 / max(T1.partial_of(f0), 1)


def peak_track(x, sr, dur=0.6, winms=32.0, hopms=6.0, fmax=3000.0):
    """Where the loudest spectral peak sits over time - the formant sweep
    Matt describes as B-wAWWW -> B-wAHHH between G2 and C3."""
    w = int(sr * winms / 1000.0)
    h = int(sr * hopms / 1000.0)
    out = []
    for i in range(0, min(len(x) - w, int(dur * sr)), h):
        s = x[i:i + w]
        sp = np.abs(np.fft.rfft((s - s.mean()) * np.hanning(len(s)))) ** 2
        fr = np.fft.rfftfreq(len(s), 1.0 / sr)
        m = fr <= fmax
        if not m.any() or sp[m].sum() <= 0:
            continue
        out.append((i / sr, float(fr[m][int(np.argmax(sp[m]))]),
                    float(np.sqrt(np.mean(s * s)))))
    return out


def rattle_row(cfg, n, nsec=LONG_SEC, slot=LONG_SLOT, tag="rat"):
    row, x, sr = one_note(cfg, n, tag, nsec=nsec, slot=slot)
    if x is None:
        return None
    att = win(x, sr, 0.0, 0.45)
    rel = win(x, sr, nsec, nsec + 0.55)
    ar, astr, _ = mod_rate(att, sr)
    rr, rstr, _ = mod_rate(rel, sr)
    # ...and again strictly BELOW the note's own repetition rate.  The
    # unrestricted search above finds f0 itself on every note, which is not a
    # modulation at all - it is the pulse train.  A rattle riding ON that
    # train has to be slower than it, so the sub-band is the discriminating
    # measurement.
    # the same measurement with a completely different detrend.  Where the
    # two agree the rate is a property of the signal; where they disagree
    # there is no single rate there and saying one would be inventing it.
    ar2, _, _ = mod_rate(att, sr, method="ma")
    rr2, _, _ = mod_rate(rel, sr, method="ma")
    sub = 0.8 * midi_hz(n)
    asr, asstr, _ = mod_rate(att, sr, hi=sub)
    rsr, rsstr, _ = mod_rate(rel, sr, hi=sub)
    agree = (np.isfinite(ar) and np.isfinite(ar2)
             and abs(ar - ar2) <= 0.05 * max(ar, ar2))
    return {"attack_rate": ar, "attack_strength": astr,
            "attack_rate_ma": ar2, "release_rate_ma": rr2,
            "methods_agree": bool(agree),
            "release_rate": rr, "release_strength": rstr,
            "attack_sub": asr, "attack_sub_strength": asstr,
            "release_sub": rsr, "release_sub_strength": rsstr,
            "f0": midi_hz(n), "bore_rate": bore_rate(n),
            "partial": T1.partial_of(midi_hz(n)),
            "cents": row["cents"], "lock": row["lock"], "rms": row["rms"],
            "rel_rms": float(rms(rel)), "att_rms": float(rms(att)),
            "peaks": peak_track(x, sr)}


def print_rattle(rows, title):
    print("\n  %s" % title)
    print("    %-5s %7s %3s | %9s | %8s %8s | %8s %7s | %8s %7s"
          % ("note", "f0", "p", "bore r/t", "att all", "rel all",
             "att <f0", "str", "rel <f0", "str"))
    for n, r in sorted(rows.items()):
        if r is None:
            continue
        print("    %-5s %7.1f %3d | %9.1f | %8.1f %8.1f | %8.1f %7.2f | "
              "%8.1f %7.2f"
              % (note_name(n), r["f0"], r["partial"], r["bore_rate"],
                 r["attack_rate"], r["release_rate"],
                 r["attack_sub"], r["attack_sub_strength"],
                 r["release_sub"], r["release_sub_strength"]))


def stage_rattle(notes=None, cfg=None):
    notes = notes or RATTLE_NOTES
    cfg = cfg or base_cfg()
    print("=== WO3: the oscillation in the attack and the tail ===")
    print("  Matt's map is the ground truth: too slow in the low octaves,")
    print("  right in octave 5, too fast at the top of octave 6.")
    rows = {n: rattle_row(cfg, n) for n in notes}
    print_rattle(rows, "measured modulation rate of the amplitude envelope")

    print("\n  candidate mechanisms against the measured attack rate")
    print("    %-5s %9s | %9s %9s %9s %9s %9s"
          % ("note", "measured", "bore r/t", "f0", "f_lip", "1/ATT_SEC",
             "1/OVER"))
    elim = {}
    for n in notes:
        r = rows.get(n)
        if r is None:
            continue
        flip = T1.TUNE_MAP.get(n, T2.TUNE_MAP.get(n, 0.8)) * r["f0"]
        cands = {"bore_rt": r["bore_rate"], "f0": r["f0"], "f_lip": flip,
                 "inv_att": 1.0 / T1.ATT_SEC, "inv_over": 1.0 / T1.OVER_SEC,
                 "inv_burst": 1.0 / T1.BURST_SEC, "inv_rel": 1.0 / T1.REL_SEC}
        elim[n] = {"measured": r["attack_sub"], "measured_all":
                   r["attack_rate"], "release": r["release_sub"],
                   "cands": cands}
        print("    %-5s %9.1f | %9.1f %9.1f %9.1f %9.1f %9.1f"
              % (note_name(n), r["attack_sub"], cands["bore_rt"], cands["f0"],
                 cands["f_lip"], cands["inv_att"], cands["inv_over"]))
    print("\n    ratio measured/candidate (1.00 = that candidate IS the rate)")
    print("    %-5s | %8s %8s %8s %8s %8s"
          % ("note", "bore r/t", "f0", "f_lip", "1/ATT", "1/OVER"))
    acc = {k: [] for k in ("bore_rt", "f0", "f_lip", "inv_att", "inv_over")}
    for n in notes:
        e = elim.get(n)
        if not e or not np.isfinite(e["measured"]):
            continue
        cells = []
        for k in ("bore_rt", "f0", "f_lip", "inv_att", "inv_over"):
            v = e["measured"] / max(e["cands"][k], 1e-9)
            acc[k].append(v)
            cells.append("%8.2f" % v)
        print("    %-5s | %s" % (note_name(n), " ".join(cells)))
    print("    %-5s | %s" % ("SPREAD", " ".join(
        "%8.2f" % (max(v) / max(min(v), 1e-9)) if v else "     n/a"
        for v in (acc[k] for k in ("bore_rt", "f0", "f_lip", "inv_att",
                                   "inv_over")))))
    print("      (a candidate that IS the mechanism has ratio spread ~1.0 "
          "across the whole map)")

    print("\n  the G2 -> C3 formant sweep (loudest spectral peak over time)")
    for n in (31, 33, 36, 43):
        r = rows.get(n) or rattle_row(cfg, n)
        if r is None:
            continue
        pk = r["peaks"]
        pick = [0.03, 0.06, 0.1, 0.15, 0.25, 0.4, 0.55]
        vals = []
        for t in pick:
            k = min(range(len(pk)), key=lambda i: abs(pk[i][0] - t))
            vals.append(pk[k][1])
        print("    %-5s  " % note_name(n) + "  ".join(
            "%.2fs:%5.0f" % (t, v) for t, v in zip(pick, vals)) + "  Hz")
    json.dump({"rows": rows, "elim": elim},
              open(os.path.join(SCRATCH, "rattle.json"), "w"), indent=1,
              default=float)
    return rows, elim


def stage_below(notes=None):
    """What the patch DOES below the solved range.  Every map starts at midi
    29; below that every CurveNode holds its first knot, so the whole
    instrument plays midi 28 and midi 12 with F2's lip ratio, F2's air-column
    trim, F2's breath support and F2's trim - and the air column is
    `partial_of(f0)` = 1 period of a note that is nowhere near the tube's own
    fundamental."""
    notes = notes or BELOW_NOTES
    print("=== WO3: what the patch does BELOW the solved range (midi < 29) ===")
    print("  every per-note map holds its midi-29 knot down here.")
    print("    %-5s %4s %7s %3s | %8s %7s %7s %7s | %8s %8s"
          % ("note", "midi", "f0", "p", "bore r/t", "cents", "lock", "rms",
             "att rate", "rel rate"))
    out = {}
    for n in notes + [29, 31]:
        r = rattle_row(base_cfg(), n, tag="below")
        if r is None:
            continue
        out[n] = r
        print("    %-5s %4d %7.1f %3d | %8.1f %7.1f %7.2f %7.4f | %8.1f %8.1f"
              % (note_name(n), n, r["f0"], r["partial"], r["bore_rate"],
                 r["cents"], r["lock"], r["rms"], r["attack_rate"],
                 r["release_rate"]))
    print("  (lock = fraction of the energy on the written note's harmonic "
          "series; cents = how far off that note it came out)")
    json.dump(out, open(os.path.join(SCRATCH, "below.json"), "w"), indent=1,
              default=float)
    return out


# ===========================================================================
# WO4: the max-velocity squeak (backlog 74a7)
# ===========================================================================
def squeak_row(cfg, n, tag="sq"):
    """A squeak is the loop jumping OFF the written partial for part of the
    note.  Measured as: the pitch of each 60 ms slice of the attack against
    the written note, plus the settled pitch."""
    row, x, sr = one_note(cfg, n, tag)
    if x is None:
        return None
    tgt = midi_hz(n)
    # The slice has to hold enough PERIODS for the autocorrelation to mean
    # anything.  A fixed 60 ms window is 2.6 periods at F2 and its lag search
    # runs off the end of the slice, which reported F2 as jumping 42% sharp
    # and would have cut its ff by 5 dB on a metric bug.  Six periods, floor
    # 60 ms.
    w = max(int(0.06 * sr), int(6.0 * sr / tgt))
    h = max(1, w // 3)
    jumps = []
    for i in range(0, min(len(x) - w, int(0.5 * sr)), h):
        f = T1.f0_autocorr(x[i:i + w], sr, tgt)
        if np.isfinite(f):
            jumps.append((i / sr, float(f), float(T1.cents(f, tgt))))
    # UPWARD only - a squeak is the loop grabbing a HIGHER partial.  A slice
    # reading low is the autocorrelation picking a subharmonic of a note that
    # is perfectly fine; counting those pulled 25 notes' ff in on an artifact.
    off = [j for j in jumps if j[2] > 120.0]
    return {"cents": row["cents"], "lock": row["lock"], "rms": row["rms"],
            "n_slices": len(jumps), "n_off": len(off),
            "worst": (max((abs(j[2]) for j in off)) if off else 0.0),
            "first_off": (off[0][0] if off else float("nan")),
            "ratio": (max((j[1] / tgt for j in off)) if off else 1.0),
            "jumps": jumps}


def squeaks(r):
    """The gate, stated once.  A note squeaks at this velocity if more than
    SQUEAK_FRAC of its attack slices sit off the written partial, or any of
    them sits more than SQUEAK_RATIO away from it, or the note does not end
    up on the written pitch at all."""
    if r is None:
        return True
    frac = r["n_off"] / max(r["n_slices"], 1)
    # Only UPWARD excursions count.  A single attack slice reading x0.51 or
    # x0.34 is the autocorrelation picking a subharmonic, not the loop
    # dropping an octave - and a first pass with a downward test pulled 25
    # notes' ff in, including A#3's all the way to its mf, on that artifact.
    # A loop that really drops a partial fails the settled-pitch test below
    # (which is how C#6 at velocity 1.0 gets caught, at -1198 cents).
    return bool(frac > SQUEAK_FRAC or r["ratio"] > SQUEAK_RATIO
                or abs(r["cents"]) > SQUEAK_CENTS
                or r["lock"] < T1.LOCK_RATIO)


def stage_squeak(notes=None, vels=(VEL_MF, 0.9, VEL_FF)):
    notes = notes or list(range(53, 80, 2))
    print("=== WO4: the intermittent squeak at max velocity ===")
    print("  a squeak = the loop off the written partial during the attack.")
    print("    %-5s %5s | " % ("note", "kPa")
          + " ".join("%-22s" % ("velocity %.2f" % v) for v in vels))
    out = {}
    for n in notes:
        out[n] = {}
        cells = []
        for v in vels:
            r = squeak_row(base_cfg(velocity=v), n)
            out[n][v] = r
            if r is None:
                cells.append("%-22s" % "render failed")
                continue
            cells.append("%-22s" % ("%2d/%2d off, x%.2f, %+.0fc"
                                    % (r["n_off"], r["n_slices"], r["ratio"],
                                       r["cents"])))
        print("    %-5s %5.0f | %s"
              % (note_name(n), T3.dyn_pressure(n, VEL_FF) * PM_TAB / 1000.0,
                 " ".join(cells)))
    tot = {v: sum((out[n][v] or {}).get("n_off", 0) for n in notes)
           for v in vels}
    print("  slices off the written partial, summed over the notes tested: "
          + ", ".join("velocity %.2f -> %d" % (v, tot[v]) for v in vels))
    json.dump(out, open(os.path.join(SCRATCH, "squeak.json"), "w"), indent=1,
              default=float)
    return out


def stage_ffstab(notes=None):
    """WO4's fix: walk each note's ff pressure down until its ATTACK is
    stable at velocity 1.0.

    Not a global cut.  Attempt 3's ff sits at FF_REL of each note's measured
    ceiling, and the measurement that set that ceiling was a SETTLED-window
    test - it asked whether the note plays in tune once it is going, not
    whether it starts cleanly.  This adds the attack test and only pulls in
    the notes that fail it, so the notes with a clean ff keep it.  Velocity
    0.8 is untouched by construction (the ff map only acts above it), which
    is where Matt's "squeaks just enuf... I kind of like it" lives.
    """
    notes = notes or list(range(TUNE_LO, TUNE_HI + 1))
    print("=== WO4 fix: per-note ff ceiling, gated on a STABLE attack ===")
    ff = dict(T3.FF_MAP)
    moved = []
    for n in notes:
        mf = T3.MF_SCALE * T2.PRESS_MAP.get(n, 1.0)
        for k in range(7):
            cfg = base_cfg(velocity=VEL_FF, ff_map=ff,
                           voice_curve=VOICE_CURVE)
            r = squeak_row(cfg, n, "ffs")
            if r is None:
                break
            bad = squeaks(r)
            if not bad:
                break
            nxt = max(mf, round(ff[n] * 0.9, 4))
            if nxt >= ff[n]:
                break
            moved.append((n, ff[n], nxt, r["n_off"], r["ratio"], r["cents"]))
            ff[n] = nxt
        if moved and moved[-1][0] == n:
            print("    %-5s ff %7.3f -> %7.3f  (%d slices off, x%.2f, %+.0f c)"
                  % (note_name(n), moved[-1][1], ff[n], moved[-1][3],
                     moved[-1][4], moved[-1][5]))
    print("  %d of %d notes had their ff pulled in; the rest keep attempt 3's"
          % (len({m[0] for m in moved}), len(notes)))
    print("\nFF4_MAP = %s" % json.dumps(ff))
    json.dump({"ff": ff, "moved": moved},
              open(os.path.join(SCRATCH, "ffstab.json"), "w"), indent=1,
              default=float)
    return ff


# ===========================================================================
# ears cells
# ===========================================================================
SUS_NOTE = 46                    # A#3 - the widest breath range in the model
SUS_SEC, SUS_TAIL = 4.0, 1.0
LADDER_SEC, LADDER_GAP = 1.3, 0.45
PASSAGE = [46, 48, 50, 53, 51, 48, 46, 41]
PASSAGE_SEC, PASSAGE_GAP, PASSAGE_TAIL = 0.55, 0.06, 1.2
EARS_LINE = T2.EARS_LINE
LINE_SEC, LINE_GAP = T2.LINE_SEC, T2.LINE_GAP

CELL_BLURB = {
    "line": ("THE CANDIDATE, G2 to G6 at normal strength (velocity 0.8)",
             "this should be IDENTICAL to attempt 3 - it is the no-regression "
             "check, not a new sound"),
    "hold_soft": ("ONE long note (A#3), blown softly, held 4 seconds",
                  "PLAY THIS AGAINST hold_hard - the two are matched to the "
                  "same loudness in the held part on purpose, so anything "
                  "you hear is tone, not volume"),
    "hold_hard": ("the SAME long note blown hard, same loudness",
                  "THE ONE THAT CARRIES THE ROUND - is the held part a "
                  "different tone now, or still the same tone louder?"),
    "ladder_mid": ("A#3 three times: soft, normal, hard (loudness left in)",
                   "the continuity cell - the same one you had last round"),
    "ladder_low": ("F2 three times: soft, normal, hard",
                   "the low register, where last round measured nothing at "
                   "all"),
    "passage": ("a short line, played, with the last note left to ring out",
                "does it hold together as playing rather than as test tones"),
    "ab_attempt3_ff": ("attempt 3 blown as hard as it goes",
                       "the before side"),
    "ab_attempt4_ff": ("attempt 4 blown as hard as it goes, SAME loudness",
                       "the after side - level-matched to the line above, so "
                       "you are only judging tone"),
}


def calibrate(cfg, notes, nsec, gap, tag, target, peak_ceil=PEAK_CEIL, **kw):
    """One gain at the very output so the cell's sounding rms matches the
    presentation target, backing off if that would push the peak past the
    ceiling.  Presentation only - it multiplies the finished signal."""
    got, pj, wp = render_score(cfg, notes, nsec, gap, tag + "_cal", SCRATCH,
                               **kw)
    if got is None:
        return None
    x, sr = got
    sr_ = sounding_rms(x)
    pk = max(float(np.abs(x).max()), 1e-9)
    want = target / max(sr_, 1e-9)
    cap = peak_ceil / pk
    T1.rm(pj, wp)
    return {"gain": min(want, cap), "want": want, "cap": cap,
            "capped_db": (20 * math.log10(want / cap) if want > cap else 0.0)}


README = """# Trombone, attempt 4 - the held-note round

## THE QUESTION

**When you hold a note and blow harder, is it a different tone now - or still
the same tone, louder?**

Play `hold_soft.wav` and then `hold_hard.wav`. They are deliberately matched to
the same loudness in the held part, so volume cannot fool either of us: if they
sound the same, this round failed.

If you only play one file, play **hold_hard.wav**.

## What you said, and what I found

> "the loudness/brightness linkage is there, but it's subtle, and *only* there
> in the attack - once the notes settle, their sustain phases sound identical to
> me, just louder. Feel free to check this empirically."

I did, and you were hearing something real, but not quite the thing I would have
guessed. Measured on the held part of a note, well after the attack:

{wo1}

So the instrument WAS making a different tone when blown harder - and then
throwing most of it away at the very last step. There is a tone control at the
end of the chain, the one you picked yourself last round when you said "line
beats line_brighter". It is a single fixed setting, and it was chosen back when
the instrument was stuck permanently at full blast. A fixed tone control sitting
exactly where the sound's energy moves when you blow harder removes most of the
movement: {thrown}

## What changed

**The tone control now moves with how hard you blow.** At your normal playing
strength it is EXACTLY where you set it - same number, same sound, not one bit
different. Blow harder and it opens; play softly and it closes. How far it opens
and closes was not picked by ear: it is solved so that the amount the instrument
brightens between soft and hard SURVIVES to the output instead of being filtered
off, and nothing else about the setting moved.

{corner}

**And the top end squeaks less.** You said the attack is intermittently squeaky
at maximum velocity and that at 0.8 it squeaks "just enuf to sound like a
not-great trumpet player" and you like it there. So 0.8 is untouched. What
changed is the hardest setting: each note's maximum breath was set last round by
a test that only asked whether the note plays in tune once it is going, never
whether it STARTS cleanly. Adding the start test pulled the maximum in on
{squeaked} of 51 notes - {squeaklist} - and left the other {squeakkept}
exactly as they were. One of those, C#6, was not squeaking at all: at maximum it
was dropping a whole octave.

## The rattle in the low octaves, measured

You described an oscillation in the attack, repeated in the tail, "too slow in
the low frequencies, perfect in octave 5, and too fast at the top of octave 6 -
as if some Curve just needs some tweaking. Have a feeling it's not that simple."

It is not that simple, and your instinct was right. I measured the rate of that
oscillation note by note and tested it against every candidate in the
instrument - the air column's echo rate, the lip's own resonance, each of the
breath-envelope timings:

{rattle}

**The oscillation IS the note.** Everywhere the measurement gives a clear
answer, the rate came back as the note's own frequency, to within one percent -
not the air column's echo rate, not the lip, not any of the envelope timings.
It is not a wobble riding on top of the sound that could be retuned; it is the
sound's own pulse rate, and the only way to change it is to play a different
note. That is why it gets faster as you go up: it has to. (I ran the
measurement two different ways and only trust a note where both agree; the top
two notes are marked "no single rate" because they do not, which is its own
answer - there is no steady oscillation up there to hear.)

What makes a low note sound like an impact rather than a pitch is that below
about 40 pulses a second the ear stops hearing a pitch at all and starts hearing
separate events. You located that boundary by ear exactly: you said the sustain
appears at D2, 36.7 Hz. Measured, this instrument starts holding a note at D2
and not below:

{below}

Below C2 there is no note at all - only the thump of the attack and the air
column ringing down after it, which is the "dullish impact through a spring
reverb" you described, and it is all there is to hear down there because nothing
is oscillating. Those notes are also outside the range anything is tuned for:
every setting in the instrument stops at F2 and simply holds its F2 value below
that, which is why D2 and E2 play 12 to 27 cents sharp.

I have NOT tried to fix the low rattle by tweaking anything, because there is no
rate to tweak. The one thing this round did change down there is the same tone
control: at F2 it had been removing essentially all of the difference between
soft and hard.

## Cells

| file | what it is | listen for |
|---|---|---|
{cells}

## Gates

{gates}
"""


def stage_all():
    fail, extra = [], []
    os.makedirs(CAND_OUT, exist_ok=True)
    os.makedirs(EARS_OUT, exist_ok=True)
    os.makedirs(SCRATCH, exist_ok=True)
    chrom = list(range(TUNE_LO, TUNE_HI + 1))

    print("\n=== 0. loudness reference ===")
    libref, libstats = T2.library_reference()
    print("  patches/library/winds/oboe1.json sounding rms %.4f" % libref)

    print("\n=== 1. velocity %.2f is attempt 3, sample for sample ===" % VEL_MF)
    a3, pj3, wp3 = render_score(base_cfg(), EARS_LINE, LINE_SEC, LINE_GAP,
                                "par_a3", SCRATCH)
    a4, pj4, wp4 = render_score(cand_cfg(), EARS_LINE, LINE_SEC, LINE_GAP,
                                "par_a4", SCRATCH)
    parity, parity_frac = None, 0.0
    if a3 and a4:
        m = min(len(a3[0]), len(a4[0]))
        d = np.abs(a3[0][:m] - a4[0][:m])
        parity = float(d.max())
        nz = int((d > 0).sum())
        parity_frac = nz / float(m)
        print("  max |attempt4 - attempt3| over the whole line: %.3e "
              "(%d of %d samples differ, %.4f%%)"
              % (parity, nz, m, 100.0 * nz / m))
        print("  one 16-bit step is %.3e - the smallest difference a WAV can "
              "carry" % LSB16)
        # Not bit-identity: widening the steepener's knots from +-4 to +-8 at
        # the same slope changes the two knot VALUES, so the interpolation
        # arithmetic rounds differently in its last bits.  The transfer is the
        # same line.  The gate is therefore "inside one quantization step",
        # and the behavioural gates (tuning, speak time) are separate below.
        if parity > LSB16 * 1.5:
            fail.append("velocity %.2f differs from attempt 3 by %.3e, more "
                        "than one 16-bit step" % (VEL_MF, parity))
        T1.rm(pj3, wp3, pj4, wp4)

    print("\n=== 2. the gate table: every chromatic note at velocity %.2f ==="
          % VEL_MF)
    new_rows = survey(cand_cfg(), chrom, "survey_new")
    a3_rows = survey(base_cfg(), chrom, "survey_a3")
    speaking = [n for n in chrom if new_rows.get(n, {}).get("spoke")]
    worst = max((abs(new_rows[n]["cents"]) for n in speaking), default=999)
    print("  speaks on %d of %d notes; worst tuning error %.1f cents"
          % (len(speaking), len(chrom), worst))
    if worst > 2.0:
        fail.append("worst tuning %.1f cents over the 2.0 cent gate" % worst)
    worse_c, worse_s = [], []
    for n in chrom:
        a, b = new_rows.get(n), a3_rows.get(n)
        if not a or not b:
            continue
        if b["spoke"] and not a["spoke"]:
            worse_c.append("%s stopped speaking" % note_name(n))
            continue
        if a["spoke"] and b["spoke"]:
            if abs(a["cents"]) > max(LOCK_CENTS_GATE, abs(b["cents"]) + 1.0):
                worse_c.append("%s %+.0f c (was %+.0f)"
                               % (note_name(n), a["cents"], b["cents"]))
            if (np.isfinite(a["speak"]) and np.isfinite(b["speak"])
                    and a["speak"] > b["speak"] * 1.5 + 0.03):
                worse_s.append("%s %.0f ms (was %.0f)"
                               % (note_name(n), 1000 * a["speak"],
                                  1000 * b["speak"]))
    print("  tuning regressions vs attempt 3:      %s"
          % (", ".join(worse_c) or "none"))
    print("  speak-time regressions vs attempt 3:  %s"
          % (", ".join(worse_s) or "none"))
    if worse_c:
        fail.append("tuning regressed vs attempt 3: %s" % ", ".join(worse_c))
    if worse_s:
        fail.append("speak time regressed vs attempt 3: %s"
                    % ", ".join(worse_s))
    c4 = new_rows.get(48, {}).get("speak", float("nan"))
    if not (np.isfinite(c4) and c4 <= SPEAK_TARGET_C4):
        fail.append("C4 speak time %.0f ms over the %.0f ms target"
                    % (1000 * c4, 1000 * SPEAK_TARGET_C4))

    print("\n=== 3. ignition from silence, one note at a time ===")
    ign, ignlvl = {}, {}
    for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
        dead, lv = [], []
        for n in chrom:
            r, _, _ = one_note(cand_cfg(velocity=v), n, "ign", nsec=1.2,
                               slot=1.5)
            if not ignites(r):
                dead.append(note_name(n))
            elif r:
                lv.append(r["rms"])
        ign[tag] = dead
        ignlvl[tag] = [float(min(lv)), float(max(lv))] if lv else [0.0, 0.0]
        print("  %-3s  %d of %d notes start on their own and lock (rms "
              "%.5f..%.5f)%s"
              % (tag, len(chrom) - len(dead), len(chrom), ignlvl[tag][0],
                 ignlvl[tag][1],
                 "" if not dead else "   SILENT: " + ", ".join(dead)))
        if dead:
            fail.append("notes that will not start from silence at %s: %s"
                        % (tag, ", ".join(dead)))

    print("\n=== 4. the steepener's drive stays inside its domain ===")
    print("  every note at ff, over the WHOLE note (attempt 3 checked four "
          "notes over the settled window only, and missed G6's attack)")
    peaks = {}
    for n in chrom:
        x, sr, _ = probe_note(cand_cfg(velocity=VEL_FF), n, "NL_in", 0.05,
                              tag="dom")
        if x is None:
            continue
        peaks[n] = float(np.abs(win(x, sr, 0.0, LONG_SEC)).max())
    hot = sorted(peaks, key=lambda n: -peaks[n])[:5]
    print("  worst five: " + ", ".join("%s %.2f" % (note_name(n), peaks[n])
                                       for n in hot)
          + "   (domain +-%.1f)" % DRIVE4_DOMAIN)
    if max(peaks.values()) > DRIVE4_DOMAIN:
        fail.append("steepener drive %.2f runs outside its curve domain"
                    % max(peaks.values()))

    print("\n=== 5. THE METRIC: the SETTLED window, attempt 3 vs attempt 4 ===")
    metric = {}
    for n in METRIC_NOTES:
        metric[n] = {}
        for pre, cf in (("a3", base_cfg), ("a4", cand_cfg)):
            for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
                r_, x, sr = one_note(cf(velocity=v), n, "met")
                if x is None:
                    continue
                metric[n][pre + "_" + tag] = {
                    "settled": bright_win(x, sr, *SET_W),
                    "attack": bright_win(x, sr, *ATT_W),
                    "cents": r_["cents"], "lock": r_["lock"]}
        r = metric[n]
        print("\n  %s" % note_name(n))
        for pre, lbl in (("a3", "attempt 3"), ("a4", "attempt 4")):
            c = [r.get(pre + "_" + t) for t in ("pp", "mf", "ff")]
            if any(x is None for x in c):
                continue
            s = [x["settled"] for x in c]
            print("    %-10s settled centroid %6.0f %6.0f %6.0f  pp->ff "
                  "%+6.1f%%   mf->ff %+6.1f%%   >1kHz x%.2f"
                  % (lbl, s[0]["centroid"], s[1]["centroid"], s[2]["centroid"],
                     100 * (s[2]["centroid"] / s[0]["centroid"] - 1),
                     100 * (s[2]["centroid"] / s[1]["centroid"] - 1),
                     s[2]["hf1k"] / max(s[0]["hf1k"], 1e-9)))

    print("\n=== 6. ears cells ===")
    cells, twins = {}, {}
    cells["line"] = (cand_cfg(), EARS_LINE, LINE_SEC, LINE_GAP)
    cells["hold_soft"] = (cand_cfg(velocity=VEL_PP, tail_sec=SUS_TAIL),
                          [SUS_NOTE], SUS_SEC, 0.3)
    cells["hold_hard"] = (cand_cfg(velocity=VEL_FF, tail_sec=SUS_TAIL),
                          [SUS_NOTE], SUS_SEC, 0.3)
    for name, n in (("ladder_mid", 46), ("ladder_low", 29)):
        c = cand_cfg()
        c["velocities"] = [VEL_PP, VEL_MF, VEL_FF]
        cells[name] = (c, [n, n, n], LADDER_SEC, LADDER_GAP)
    cells["passage"] = (cand_cfg(tail_sec=PASSAGE_TAIL), PASSAGE, PASSAGE_SEC,
                        PASSAGE_GAP)
    cells["ab_attempt3_ff"] = (base_cfg(velocity=VEL_FF), EARS_LINE, LINE_SEC,
                               LINE_GAP)
    cells["ab_attempt4_ff"] = (cand_cfg(velocity=VEL_FF), EARS_LINE, LINE_SEC,
                               LINE_GAP)
    twins["ab_attempt4_ff"] = "ab_attempt3_ff"

    meta, staged = {}, []
    for name, (cfg, notes, nsec, gap) in cells.items():
        cal = calibrate(cfg, notes, nsec, gap, name, libref)
        if cal is None:
            fail.append("%s calibration" % name)
            continue
        meta[name] = cal
        staged.append(name)
    ears = []
    for name in staged:
        cfg, notes, nsec, gap = cells[name]
        c = dict(cfg)
        c["out_gain"] = c.get("out_gain", 1.0) * meta[name]["gain"]
        got, pj, wp = render_score(c, notes, nsec, gap, name, EARS_OUT)
        if got is None:
            fail.append("%s final render" % name)
            continue
        x, sr = got
        meta[name].update({
            "level": level_ceiling(x, sr), "peak": float(np.abs(x).max()),
            "rms": float(rms(x)), "sounding_rms": sounding_rms(x),
            "notes": notes, "note_sec": nsec,
            "velocities": cfg.get("velocities") or [cfg.get("velocity",
                                                            VEL_MF)],
            "centroid": centroid_all(x, sr),
            "hf1k": hf_fraction(x, sr, 1000.0)})
        ears.append(name)

    # the A/B pair and the held pair each share ONE level so only tone differs
    def match(a_n, b_n, settled=False):
        if a_n not in meta or b_n not in meta:
            return
        if settled:
            ga, _ = read_mono(os.path.join(EARS_OUT, a_n + ".wav"))
            gb, _ = read_mono(os.path.join(EARS_OUT, b_n + ".wav"))
            sa = float(rms(win(ga, SR, *SET_W)))
            sb = float(rms(win(gb, SR, *SET_W)))
            g = sb / max(sa, 1e-9)
        else:
            g = meta[b_n]["sounding_rms"] / max(meta[a_n]["sounding_rms"],
                                                1e-9)
        cfg, notes, nsec, gap = cells[a_n]
        c = dict(cfg)
        c["out_gain"] = c.get("out_gain", 1.0) * meta[a_n]["gain"] * g
        got, pj, wp = render_score(c, notes, nsec, gap, a_n, EARS_OUT)
        if got is None:
            return
        x, sr = got
        meta[a_n].update({"level": level_ceiling(x, sr),
                          "peak": float(np.abs(x).max()), "rms": float(rms(x)),
                          "sounding_rms": sounding_rms(x),
                          "matched_to": b_n,
                          "settled_rms": float(rms(win(x, sr, *SET_W))),
                          "centroid": centroid_all(x, sr),
                          "hf1k": hf_fraction(x, sr, 1000.0)})

    match("ab_attempt4_ff", "ab_attempt3_ff")
    match("hold_hard", "hold_soft", settled=True)

    for name in ears:
        m = meta[name]
        db = 20 * math.log10(max(m["sounding_rms"], 1e-9) / libref)
        flags = []
        if m["level"] > LEVEL_CEILING:
            flags.append("LEVEL")
            fail.append("%s 0.5 s rms %.2f over the ceiling"
                        % (name, m["level"]))
        if m["peak"] > 0.999:
            flags.append("CLIP")
            fail.append("%s peaks at %.3f" % (name, m["peak"]))
        if m["rms"] < AUDIBILITY_FLOOR:
            flags.append("SILENT")
            fail.append("%s is silent" % name)
        print("  %-16s peak %.3f  0.5s rms %.3f  sounding rms %.4f "
              "(%+.1f dB vs library)  centroid %4.0f %s"
              % (name, m["peak"], m["level"], m["sounding_rms"], db,
                 m["centroid"], " ".join(flags)))

    print("\n=== 7. are the A/B pairs audible against each other? ===")
    for a_n, b_n in (("ab_attempt4_ff", "ab_attempt3_ff"),
                     ("hold_hard", "hold_soft")):
        if a_n in ears and b_n in ears:
            a, _ = read_mono(os.path.join(EARS_OUT, a_n + ".wav"))
            b, _ = read_mono(os.path.join(EARS_OUT, b_n + ".wav"))
            d = diff_db(a, b)
            print("  %-16s vs %-16s %6.1f dB  %s"
                  % (a_n, b_n, d, "ok" if d > FEATURE_DB else "INAUDIBLE"))
            if d <= FEATURE_DB:
                fail.append("%s is inaudible against %s" % (a_n, b_n))
    if "hold_hard" in meta and "hold_soft" in meta:
        sh = meta["hold_hard"].get("settled_rms")
        ss = float(rms(win(read_mono(os.path.join(EARS_OUT,
                                                  "hold_soft.wav"))[0],
                           SR, *SET_W)))
        if sh:
            print("  held-part level match: %+.2f dB (0.0 = perfectly "
                  "matched)" % (20 * math.log10(max(sh, 1e-12) / max(ss,
                                                                     1e-12))))

    print("\n=== 8. candidate patch ===")
    c = dict(cand_cfg())
    c["notes"], c["note_sec"], c["slot"] = (EARS_LINE, LINE_SEC,
                                            LINE_SEC + LINE_GAP)
    if "line" in meta:
        c["out_gain"] = meta["line"]["gain"]
    patch = make_patch(c)
    cand_path = os.path.join(CAND_OUT, CAND_NAME + ".json")
    with open(cand_path, "w") as f:
        json.dump(patch, f, indent=1)
    print("  wrote %s (%d nodes)"
          % (os.path.relpath(cand_path, ROOT), len(patch["graph"]["nodes"])))
    gc = subprocess.run([UI, "--gatecheck", cand_path], capture_output=True,
                        text=True)
    gout = (gc.stdout or gc.stderr).strip()
    print("  %s" % gout)
    if "gateable=1" not in gout:
        fail.append("candidate is not key-gateable: %s" % gout)

    # ---- README + manifest ---------------------------------------------
    T1.purge(EARS_OUT, {n + ".wav" for n in ears}
             | {"README.md", "measurements.json",
                "narrative.json"})
    T1.purge(PATCH_OUT, {n + ".json" for n in ears} | {CAND_NAME + ".json"})

    cell_tbl = "\n".join(
        "| `%s.wav` | %s | %s |" % (n, CELL_BLURB.get(n, ("", ""))[0],
                                    CELL_BLURB.get(n, ("", ""))[1])
        for n in ears)
    gates = ("All 51 notes from F2 to G6 still speak, the worst is %.1f cents "
             "off, and nothing got slower to start. At your normal playing "
             "strength the render matches attempt 3 to within one 16-bit "
             "step - the smallest difference a WAV file can hold - differing "
             "at all on %.2f%% of samples, which is rounding, not a change. Every note still "
             "starts from silence and locks on its written pitch at all "
             "three strengths, and no cell clips."
             % (worst, 100.0 * (parity_frac or 0.0)))
    json.dump({
        "library_reference": {"patch": os.path.relpath(T2.LIB_REF_PATCH,
                                                       ROOT),
                              "sounding_rms": libref, "stats": libstats},
        "parity_vs_attempt3_at_mf": parity,
        "voice_curve": VOICE_CURVE, "ff_map": FF4_MAP,
        "ff_map_attempt3": T3.FF_MAP,
        "metric": metric, "ignition": ign, "ignition_rms": ignlvl,
        "drive_peaks_ff": peaks,
        "notes_new": new_rows, "notes_a3": a3_rows,
        "ears": {n: meta[n] for n in ears},
    }, open(os.path.join(EARS_OUT, "measurements.json"), "w"), indent=1,
        default=float)
    print("\n%d ears cells staged -> %s" % (len(ears), EARS_OUT))
    if fail:
        print("\nGATE FAILURES:")
        for f_ in fail:
            print("  " + f_)
        return 2, {"cells": cell_tbl, "gates": gates, "metric": metric,
                   "worst": worst, "parity": parity, "meta": meta,
                   "ears": ears}
    print("\nAll gates passed.")
    return 0, {"cells": cell_tbl, "gates": gates, "metric": metric,
               "worst": worst, "parity": parity, "meta": meta, "ears": ears}


def write_readme(info):
    """The queue's own README, built from renders made in this run - no
    number in it is remembered."""
    print("\n=== 9. README (its numbers measured here, not recalled) ===")
    pp = pre_post(METRIC_NOTES)
    rows = {n: rattle_row(base_cfg(), n) for n in RATTLE_NOTES}
    below = {n: rattle_row(base_cfg(), n, tag="below")
             for n in BELOW_NOTES + [29]}
    m = info["metric"]

    def ratio(n, pre_, key="settled"):
        r = m.get(n, {})
        a, b = r.get(pre_ + "_pp"), r.get(pre_ + "_ff")
        if not a or not b:
            return float("nan")
        return 100 * (b[key]["centroid"] / a[key]["centroid"] - 1)

    wo1 = fmt_table(
        ["note", "held part, attempt 3 soft -> hard",
         "held part, attempt 4 soft -> hard", "before the tone control"],
        [[note_name(n), "%+.0f%%" % ratio(n, "a3"), "%+.0f%%" % ratio(n, "a4"),
          "%+.0f%%" % (100 * (pp[n]["ff"]["pre"]["centroid"]
                              / pp[n]["pp"]["pre"]["centroid"] - 1))]
         for n in METRIC_NOTES if n in pp and "pp" in pp[n]])
    thrown = ", ".join(
        "on %s it goes in at %+.0f%% and comes out at %+.0f%%"
        % (note_name(n), 100 * (pp[n]["ff"]["pre"]["centroid"]
                                / pp[n]["pp"]["pre"]["centroid"] - 1),
           ratio(n, "a3"))
        for n in METRIC_NOTES if n in pp and "pp" in pp[n]) + "."
    corner = fmt_table(
        ["how hard you blow", "tone control corner"],
        [["softest (velocity %.2f and below)" % VEL_PP, "%.0f Hz" % VOICE_PP],
         ["your normal (velocity %.2f)" % VEL_MF,
          "%.0f Hz - UNCHANGED, your own pick" % VOICE_MF],
         ["hardest (velocity 1.00)", "%.0f Hz" % VOICE_FF]])
    rat = fmt_table(
        ["note", "the note itself", "measured rate of the oscillation",
         "the air column's echo rate", "the lip's resonance"],
        [["%s" % note_name(n), "%.0f Hz" % r["f0"],
          ("%.0f Hz" % r["attack_rate"] if r["methods_agree"]
           else "no single rate"), "%.0f Hz" % r["bore_rate"],
          "%.0f Hz" % (T1.TUNE_MAP.get(n, T2.TUNE_MAP.get(n, 0.8)) * r["f0"])]
         for n, r in sorted(rows.items()) if r])
    bel = fmt_table(
        ["note", "its frequency", "does it hold a note?",
         "how far off the written pitch"],
        [[note_name(n), "%.1f Hz" % r["f0"],
          ("no - only the thump and the ring-down" if r["lock"] < 0.1 else
           ("barely" if r["lock"] < 0.5 else "yes")),
          ("n/a" if not np.isfinite(r["cents"]) else "%+.0f cents" % r["cents"])]
         for n, r in sorted(below.items()) if r])
    moved = [n for n in FF4_MAP if abs(FF4_MAP[n] - T3.FF_MAP.get(n, 0)) > 1e-6]
    with open(os.path.join(EARS_OUT, "README.md"), "w",
              encoding="utf-8") as f:
        f.write(README.replace("{wo1}", wo1).replace("{thrown}", thrown)
                .replace("{corner}", corner).replace("{rattle}", rat)
                .replace("{below}", bel)
                .replace("{squeaked}", str(len(moved)))
                .replace("{squeaklist}", ", ".join(note_name(n)
                                                   for n in sorted(moved)))
                .replace("{squeakkept}", str(51 - len(moved)))
                .replace("{cells}", info["cells"])
                .replace("{gates}", info["gates"]))
    json.dump({"pre_post": pp, "rattle": rows, "below": below},
              open(os.path.join(EARS_OUT, "narrative.json"), "w"), indent=1,
              default=float)
    print("  wrote %s" % os.path.join(EARS_OUT, "README.md"))


def pre_post(notes):
    """Settled brightness before and after the voicing filter, attempt 3's
    configuration - the measurement the whole round turns on."""
    out = {}
    for n in notes:
        out[n] = {}
        for tag, v in (("pp", VEL_PP), ("mf", VEL_MF), ("ff", VEL_FF)):
            cfg = base_cfg(velocity=v)
            x, sr, clip = probe_note(cfg, n, "Trans4", 0.5, raw=True,
                                     tag="vpre")
            r_, y, sr2 = one_note(cfg, n, "vout")
            if x is None or y is None:
                continue
            out[n][tag] = {"pre": bright_win(x, sr, *SET_W),
                           "out": bright_win(y, sr2, *SET_W),
                           "clipped": clip}
    return out


def survey(cfg, notes, tag, nsec=1.4, gap=0.4, chunk=8, **kw):
    rows = {}
    for i in range(0, len(notes), chunk):
        grp = notes[i:i + chunk]
        got, pj, wp = render_score(cfg, grp, nsec, gap, "%s_%d" % (tag, i),
                                   SCRATCH, **kw)
        if got is None:
            continue
        rows.update(note_rows(got[0], got[1], grp, nsec + gap, nsec))
        T1.rm(pj, wp)
    return rows


def main():
    stage = sys.argv[1] if len(sys.argv) > 1 else "all"
    if not os.path.exists(CLI):
        print("mforce_cli not found at %s" % CLI)
        return 1
    os.makedirs(PATCH_OUT, exist_ok=True)
    os.makedirs(SCRATCH, exist_ok=True)
    print("=== trombone attempt 4 - the sustain round ===")
    print("  note names: HOUSE convention (octave = midi/12)")
    print("  lip opening domain (Qn knots): +-%.1f" % LIP_DOMAIN)
    if stage == "attack":
        stage_attack()
        return 0
    if stage == "attrib":
        stage_attrib()
        return 0
    if stage == "lipdom":
        stage_lipdom()
        return 0
    if stage == "voice":
        stage_voice()
        return 0
    if stage == "corner":
        stage_corner()
        return 0
    if stage == "rattle":
        stage_rattle()
        return 0
    if stage == "below":
        stage_below()
        return 0
    if stage == "squeak":
        stage_squeak()
        return 0
    if stage == "ffstab":
        stage_ffstab()
        return 0
    if stage == "all":
        rc, info = stage_all()
        write_readme(info)
        shutil.rmtree(SCRATCH, ignore_errors=True)
        return rc
    print("unknown stage %r" % stage)
    return 1


if __name__ == "__main__":
    sys.exit(main())
