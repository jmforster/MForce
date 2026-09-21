#!/usr/bin/env python3
"""articulation1 v2 — per-note phrasing (spec 2026-09-20-note-onsets-v2).

Teaches two instruments the `tongue` onset (zero engine code, pure patch
wiring), declares them `sustaining`, then renders Ode-to-Joy
phrased-vs-flat A/B pairs plus a repeated-note hold cell per instrument.

What changed from v1 (2026-09-19):
  * instrument block declares "onsets": ["breath","tongue","slur"] and
    "sustaining": true. `breath` and `slur` are declared but UNWIRED —
    the fresh-voice attack IS the breath gesture (main envelope), and
    under v2 the legato glide IS the slur; neither needs a gesture.
  * score events carry the two per-note facts, "onset" and "hold",
    instead of the retired "phrase":"cont" grouping. Emission mirrors
    spec §3: hold on every note but a phrase's last; first note
    "breath", later notes "tongue" on a repeated pitch and "slur" on a
    pitch change (a repeated pitch physically cannot be slurred).
  * the v1 maxSec phrase-hygiene clamps are REVERTED. They existed
    because a v1 phrase was one long note to every percent stage; under
    v2 an envelope's timebase is always the NOTE, so there is nothing to
    pin.
  * the OBOE's tongue moves POST-LOOP — an output-side dent instead of
    an in-loop drive dip. Measured 2026-09-20: dipping the drive inside
    a marginal feedback loop is dropout roulette (9 of 28 boundaries
    died, one re-ignited an octave up). Dented after the loop, the same
    consonant cannot extinguish the oscillation. The trombone's lip
    mechanism measured no dropouts, so it keeps its breath-side tongue.
  * dip depth calibrated to the reference player: repeated-note dents
    measured 7-23 dB over 40-60 ms, so the default sits mid-range at
    about -12 dB over ~58 ms.

Usage:
  python tools/gen_articulation1.py            # teach + render + README
"""
import json, math, os, struct, subprocess, sys, wave

R = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CLI = os.path.join(R, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")
SRC = {
    "trombone": os.path.join(R, "patches", "audition", "trombone1",
                             "trombone_attempt4.json"),
    "oboe": os.path.join(R, "patches", "library", "winds", "oboe1.json"),
}
PATCH_DIR = os.path.join(R, "patches", "audition", "articulation1")
OUT = os.path.join(R, "renders", "dsp", "audition", "articulation1")
SCRATCH = os.path.join(R, "renders", "scratch", "articulation1")
LIB_REF = os.path.join(R, "patches", "library", "winds", "oboe1.json")
LIB_REF_FALLBACK = 0.1761          # recorded oboe1 sounding rms
PEAK_CEIL = 0.85
Q = 0.4                            # quarter note seconds (bpm 150)
TAIL = 1.2                         # trailing window so voice tails complete

# The tongue consonant, stated once: 8 ms close to DIP_DEPTH, 30 ms there,
# 20 ms reopen, idle at 1.0. Multiplied into the path it shapes, so
# 1.0 = "not tonguing". Depth 0.25 ~ -12 dB, the middle of the reference
# player's measured 7-23 dB repeated-note dents (2026-09-20).
DIP_DEPTH = 0.25
DIP_STAGES = [
    {"startVal": 1.0, "endVal": DIP_DEPTH, "type": "Linear",
     "percent": 0.008},
    {"startVal": DIP_DEPTH, "endVal": DIP_DEPTH, "type": "Linear",
     "percent": 0.030},
    {"startVal": DIP_DEPTH, "endVal": 1.0, "type": "Linear",
     "percent": 0.020},
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.0},
]
ONSETS = ["breath", "tongue", "slur"]   # spec: Onset [breath|tongue|slur];
# breath and slur are declared-but-unwired: the fresh-voice attack IS the
# breath gesture (main envelope), and the glide IS the slur.

# ---------------------------------------------------------------------------
# wav helpers
# ---------------------------------------------------------------------------

def read_mono(path):
    w = wave.open(path, "rb")
    n, ch = w.getnframes(), w.getnchannels()
    raw = struct.unpack("<%dh" % (n * ch), w.readframes(n))
    w.close()
    return [raw[i * ch] / 32768.0 for i in range(n)], w.getframerate()

def write_mono_pair_scaled(src, dst, gain):
    w = wave.open(src, "rb")
    params = w.getparams()
    n, ch = w.getnframes(), w.getnchannels()
    raw = list(struct.unpack("<%dh" % (n * ch), w.readframes(n)))
    w.close()
    out = [max(-32768, min(32767, int(round(s * gain)))) for s in raw]
    o = wave.open(dst, "wb")
    o.setparams(params)
    o.writeframes(struct.pack("<%dh" % len(out), *out))
    o.close()

def rms(x):
    return math.sqrt(sum(s * s for s in x) / len(x)) if x else 0.0

def sounding_rms(x):
    nz = [s for s in x if abs(s) > 1e-4]
    return rms(nz)

# ---------------------------------------------------------------------------
# teaching (patch JSON surgery)
# ---------------------------------------------------------------------------

GESTURE_NODES = ("__perf_t", "NGt", "TDip", "BreathT", "OutT")

def _gesture_chain(breath_ref, wired):
    """The four gesture nodes. `wired` False leaves TDip's trigger
    unconnected — the no-gesture control for the dormancy gate."""
    dip = {"id": "TDip", "type": "Envelope",
           "params": {"timeMode": "seconds", "stages": DIP_STAGES}}
    if wired:
        dip["params"]["trigger"] = {"ref": "NGt"}
    return [
        {"id": "__perf_t", "type": "PerformNode",
         "params": {"field": "onset"}},
        {"id": "NGt", "type": "NameGate",
         "params": {"name": "tongue", "in": {"ref": "__perf_t"}}},
        dip,
        {"id": breath_ref[0], "type": "CombinedSource",
         "params": {"operation": 1, "gainAdj": 0.0,
                    "source1": {"ref": breath_ref[1]},
                    "source2": {"ref": "TDip"}}},
    ]

def teach_breath_side(src_path, dst_path, breath_id, consumer_id,
                      consumer_key, wired=True):
    """Trombone placement: the dent rides the BREATH. Repoint
    consumer_id.params[consumer_key] from breath_id to BreathT =
    breath * TDip."""
    p = _load_instrument(src_path)
    nodes = p["graph"]["nodes"]
    ids = {n["id"] for n in nodes}
    for nid in GESTURE_NODES:
        assert nid not in ids, nid + " already exists in " + src_path
    # Plain refs resolve in file order (only taps may point forward), so
    # the new chain must sit BEFORE its consumer; the breath node it
    # multiplies is by construction earlier still.
    cons_idx = next(i for i, n in enumerate(nodes) if n["id"] == consumer_id)
    breath_idx = next(i for i, n in enumerate(nodes) if n["id"] == breath_id)
    assert breath_idx < cons_idx, (breath_id, consumer_id)
    nodes[cons_idx:cons_idx] = _gesture_chain(("BreathT", breath_id), wired)
    cons = {n["id"]: n for n in nodes}[consumer_id]
    ref = cons["params"][consumer_key]
    assert ref == {"ref": breath_id}, (consumer_id, consumer_key, ref)
    cons["params"][consumer_key] = {"ref": "BreathT"}
    return _write(p, dst_path)

def teach_post_loop(src_path, dst_path, wired=True):
    """Oboe placement: the dent rides the OUTPUT, after the feedback loop.
    Interpose OutT = <old output> * TDip and repoint graph.output to it,
    so the consonant can never starve the loop's ignition margin."""
    p = _load_instrument(src_path)
    g = p["graph"]
    nodes = g["nodes"]
    ids = {n["id"] for n in nodes}
    for nid in GESTURE_NODES:
        assert nid not in ids, nid + " already exists in " + src_path
    old_out = g["output"]
    assert old_out in ids, old_out
    nodes.extend(_gesture_chain(("OutT", old_out), wired))
    g["output"] = "OutT"
    return _write(p, dst_path)

def _load_instrument(src_path):
    p = json.load(open(src_path))
    p.pop("score", None)           # instrument patch, no smoke score
    inst = p.setdefault("instrument", {})
    inst["onsets"] = list(ONSETS)  # `slur` declared, deliberately unwired
    inst["sustaining"] = True      # spec §2: phrasing applies to this one
    return p

def _write(p, dst_path):
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    json.dump(p, open(dst_path, "w"), indent=1)
    return dst_path

# ---------------------------------------------------------------------------
# scores — the Performer emission rule, mirrored in python (spec §3)
# ---------------------------------------------------------------------------

def otj(base):
    """Ode to Joy, two lines, each line = ONE phrase (one breath).
    Returns [(midi, beats, phraseStart)]."""
    line = [(4, 1), (4, 1), (5, 1), (7, 1), (7, 1), (5, 1), (4, 1), (2, 1),
            (0, 1), (0, 1), (2, 1), (4, 1)]
    l1 = line + [(4, 1.5), (2, 0.5), (2, 2)]
    l2 = line + [(2, 1.5), (0, 0.5), (0, 2)]
    out = []
    for ln in (l1, l2):
        for i, (semi, beats) in enumerate(ln):
            out.append((base + semi, beats, i == 0))
    return out

def hold(midi, count=8):
    return [(midi, 1, i == 0) for i in range(count)]

def score_events(seq, phrased):
    """Stamp onset/hold per spec §3. hold on every note but a phrase's
    last; first note breath, later notes tongue on a repeated pitch and
    slur on a pitch change."""
    ev, t = [], 0.0
    for i, (midi, beats, start) in enumerate(seq):
        d = beats * Q
        e = {"note": float(midi), "velocity": 0.8, "duration": d,
             "time": round(t, 6)}
        if phrased:
            last = (i + 1 >= len(seq)) or seq[i + 1][2]
            e["hold"] = not last
            e["onset"] = ("breath" if start else
                          "tongue" if seq[i - 1][0] == midi else "slur")
        ev.append(e)
        t += d
    return ev, t

def boundaries(seq):
    """(time, repeated_pitch) for every in-phrase boundary."""
    out, t = [], 0.0
    for i, (midi, beats, _) in enumerate(seq):
        t += beats * Q
        if i + 1 < len(seq) and not seq[i + 1][2]:
            out.append((t, seq[i + 1][0] == midi))
    return out

# ---------------------------------------------------------------------------
# render pipeline
# ---------------------------------------------------------------------------

def render(patch_path, seq, phrased, tag):
    p = json.load(open(patch_path))
    ev, total = score_events(seq, phrased)
    p["score"] = ev
    p["seconds"] = total + TAIL
    os.makedirs(SCRATCH, exist_ok=True)
    pj = os.path.join(SCRATCH, tag + ".json")
    wp = os.path.join(SCRATCH, tag + ".wav")
    json.dump(p, open(pj, "w"))
    r = subprocess.run([CLI, pj, wp], capture_output=True, text=True)
    if r.returncode != 0:
        print("RENDER FAILED", tag)
        print(r.stderr[-800:])
        sys.exit(1)
    x, sr = read_mono(wp)
    return x, sr, wp

def library_reference():
    x, _, wp = render(LIB_REF, [(57, 2, True)], False, "libref")
    v = sounding_rms(x)
    os.remove(wp)
    return v if v > 0.01 else LIB_REF_FALLBACK

def deep_dropouts(x, sr, bnds):
    """The boundary-trough counter (2026-09-20 diagnosis). A boundary is a
    DEEP dropout when the line has not come back 60-200 ms later — the
    consonant is over by then, so more than 20 dB down means the
    oscillation died rather than being dented. Returns (count, worst dB)."""
    count, worst = 0, 0.0
    for t, _ in bnds:
        pre = rms(x[int((t - 0.12) * sr):int((t - 0.01) * sr)])
        post = rms(x[int((t + 0.06) * sr):int((t + 0.20) * sr)])
        if pre < 1e-5:
            continue
        db = 20 * math.log10(max(post, 1e-9) / pre)
        worst = min(worst, db)
        if db < -20.0:
            count += 1
    return count, worst

def dent_db(x, sr, bnds, repeated):
    """Mean dB of the post-boundary dent, over boundaries whose repeated-
    pitch status matches `repeated`."""
    vals = []
    for t, rep in bnds:
        if rep != repeated:
            continue
        pre = rms(x[int((t - 0.06) * sr):int((t - 0.01) * sr)])
        dip = rms(x[int((t + 0.010) * sr):int((t + 0.045) * sr)])
        if pre > 1e-5:
            vals.append(20 * math.log10(max(dip, 1e-9) / pre))
    return (sum(vals) / len(vals)) if vals else 0.0

def main():
    os.makedirs(OUT, exist_ok=True)
    target = library_reference()
    print("presentation target (oboe1 sounding rms): %.4f" % target)

    taught, control = {}, {}
    taught["trombone"] = teach_breath_side(
        SRC["trombone"], os.path.join(PATCH_DIR, "trombone_tongue.json"),
        "Mouth", "Pm", "source2")
    control["trombone"] = teach_breath_side(
        SRC["trombone"], os.path.join(SCRATCH, "trombone_nogest.json"),
        "Mouth", "Pm", "source2", wired=False)
    taught["oboe"] = teach_post_loop(
        SRC["oboe"], os.path.join(PATCH_DIR, "oboe_tongue.json"))
    control["oboe"] = teach_post_loop(
        SRC["oboe"], os.path.join(SCRATCH, "oboe_nogest.json"), wired=False)

    base = {"trombone": 48, "oboe": 60}       # house C4 / C5
    holdn = {"trombone": 46, "oboe": 64}      # A#3 / E5

    cells, stats = [], {}
    for inst in ("trombone", "oboe"):
        patch = taught[inst]
        seq = otj(base[inst])
        bnds = boundaries(seq)
        for phrased in (False, True):
            tag = "otj_%s_%s" % (inst, "phrased" if phrased else "flat")
            x, sr, wp = render(patch, seq, phrased, tag)
            g = min(target / max(sounding_rms(x), 1e-9),
                    PEAK_CEIL / max(max(abs(s) for s in x), 1e-9))
            write_mono_pair_scaled(wp, os.path.join(OUT, tag + ".wav"), g)
            st = {"srms": sounding_rms(x), "gain": g}
            if phrased:
                st["dropouts"], st["worst_db"] = deep_dropouts(x, sr, bnds)
                st["dent_rep"] = dent_db(x, sr, bnds, True)
                st["dent_slur"] = dent_db(x, sr, bnds, False)
            stats[tag] = st
            cells.append(tag)
            os.remove(wp)

        # Dormancy control: the SAME taught patch with TDip's trigger
        # unwired, on the flat score. Byte-identical means the onset
        # machinery costs nothing when nothing fires.
        xa, _, wa = render(patch, seq, False, "dorm_%s_a" % inst)
        xb, _, wb = render(control[inst], seq, False, "dorm_%s_b" % inst)
        stats["dorm_" + inst] = (xa == xb)
        os.remove(wa); os.remove(wb)

        tag = "phrase_hold_%s" % inst
        hseq = hold(holdn[inst])
        x, sr, wp = render(patch, hseq, True, tag)
        g = min(target / max(sounding_rms(x), 1e-9),
                PEAK_CEIL / max(max(abs(s) for s in x), 1e-9))
        write_mono_pair_scaled(wp, os.path.join(OUT, tag + ".wav"), g)
        hb = boundaries(hseq)
        stats[tag] = {"srms": sounding_rms(x), "gain": g,
                      "dropouts": deep_dropouts(x, sr, hb)[0],
                      "dent_rep": dent_db(x, sr, hb, True)}
        cells.append(tag)
        os.remove(wp)

    # ---- machine gates -----------------------------------------------
    for inst in ("trombone", "oboe"):
        p = stats["otj_%s_phrased" % inst]
        print("%-8s phrased: deep dropouts %d (worst %.1f dB), "
              "dent repeated %.1f dB, slurred %.1f dB"
              % (inst, p["dropouts"], p["worst_db"], p["dent_rep"],
                 p["dent_slur"]))
        print("%-8s hold cell: deep dropouts %d, dent %.1f dB"
              % (inst, stats["phrase_hold_%s" % inst]["dropouts"],
                 stats["phrase_hold_%s" % inst]["dent_rep"]))
        print("%-8s dormancy (flat == trigger-unwired control): %s"
              % (inst, stats["dorm_" + inst]))

    assert stats["otj_oboe_phrased"]["dropouts"] == 0, \
        "oboe phrased line has deep dropouts — post-loop placement failed"
    assert stats["phrase_hold_oboe"]["dropouts"] == 0, \
        "oboe hold cell has deep dropouts"
    # The dent gate proves the consonant EXISTS at repeated pitches and
    # does not exist at slurred ones. It is not calibrated to the -12 dB
    # drive depth, because what reaches the output depends on the
    # instrument: the oboe's post-loop dent passes through at full depth,
    # while the trombone's bore rings straight through a breath-side dip
    # and delivers about a tenth of it (v1's trombone gate was a <0.97
    # ratio for the same reason).
    for inst in ("trombone", "oboe"):
        assert stats["dorm_" + inst], \
            inst + ": gesture is not dormant on an unphrased render"
        assert stats["otj_%s_phrased" % inst]["dent_rep"] < -0.5, \
            inst + ": tongue dent not measurable at repeated pitches"
        assert (stats["otj_%s_phrased" % inst]["dent_slur"] >
                stats["otj_%s_phrased" % inst]["dent_rep"] + 0.5), \
            inst + ": slurred boundaries dent as much as tongued ones"

    readme = open(os.path.join(OUT, "README.md"), "w")
    readme.write(README_TMPL.format(
        tr_dent=abs(stats["otj_trombone_phrased"]["dent_rep"]),
        ob_dent=abs(stats["otj_oboe_phrased"]["dent_rep"]),
        ob_slur=abs(stats["otj_oboe_phrased"]["dent_slur"])))
    readme.close()
    print("cells:", ", ".join(cells))
    print("queue at", OUT)

README_TMPL = """# articulation1 v2 - one breath per line

Ode to Joy, two lines. In the `flat` files every note is its own breath -
the instrument starts cold thirty times, which is what you have been
hearing all along. In the `phrased` files each LINE is one breath: the
first note starts the air, and every later note rides the same moving air
column. What happens at each later note now depends on the note:

* **same pitch as the one before** - a tongue stroke. A player cannot
  slur a repeated note; the tongue is the only way to say it again.
* **a different pitch** - no tongue at all. The pitch simply slides to
  the new note over about 15 ms, which is what a slur is.

## THE QUESTION

**Play `otj_oboe_phrased.wav`. Does it sound like one breath per line,
with tongues only on the repeated notes and clean slurs between
pitches?** Then the same file for the trombone, and the `flat` pair of
each if you want the before-and-after.

If you play only one file, play `otj_oboe_phrased.wav`.

`phrase_hold_oboe.wav` / `phrase_hold_trombone.wav` are the isolated
consonant: the same note eight times on one breath - all you should hear
between repeats is the tongue, never a restart.

Measured, so you know the mechanism is on. At a repeated note the sound
dents about {ob_dent:.0f} dB on the oboe and {tr_dent:.0f} dB on the
trombone over roughly 50 ms - the middle of the range a real player
produces. At a slurred boundary the oboe dents only {ob_slur:.0f} dB,
which is the pitch moving, not a tongue. And nowhere in either phrased
line does the sound drop out: that was the failure mode last time, and
it is why the oboe's tongue now dents the sound AFTER the resonating
tube instead of throttling the air going into it.

The tongue itself is one editable shape in each patch (node `TDip`: the
path closes to a quarter for 30 ms and reopens) - if the consonant reads
too hard or too soft, that envelope is the whole knob. The patches are
`patches/audition/articulation1/{{trombone,oboe}}_tongue.json`; the
originals they extend are untouched.
"""

if __name__ == "__main__":
    main()
