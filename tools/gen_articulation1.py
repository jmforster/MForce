#!/usr/bin/env python3
"""articulation1 — first phrased renders (spec 2026-09-19-note-transitions).

Teaches two instruments the `tongue` transition (zero engine code, pure
patch wiring), then renders Ode-to-Joy phrased-vs-flat A/B pairs plus a
repeated-note hold cell per instrument.

Teaching = four nodes inserted into a COPY of each patch (originals
untouched): a Note node's `transition` pin -> NameGate("tongue") -> a
seconds-mode dip Envelope's `trigger`; the dip multiplies into the patch's
own breath path:
  trombone_attempt4: Pm.source2 (Mouth pressure envelope) -> Mouth * TDip
  oboe1:             Drive_vib.frequency (Drive_env)      -> Drive_env * TDip

Usage:
  python tools/gen_articulation1.py            # teach + render + README
"""
import json, math, os, shutil, struct, subprocess, sys, wave

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

# The tongue consonant, stated once: 8 ms dip to DIP_DEPTH, 22 ms there,
# 30 ms reopen, idle at 1.0. Multiplied into the breath, so 1.0 = "not
# tonguing". Depth 0.6 is Matt's ear (2026-09-19 interactive pass:
# "way closer to desired effect" than the full close — a full close
# reads as a restart, and on marginal loop patches can kill ignition).
DIP_DEPTH = 0.6
DIP_STAGES = [
    {"startVal": 1.0, "endVal": DIP_DEPTH, "type": "Linear",
     "percent": 0.008},
    {"startVal": DIP_DEPTH, "endVal": DIP_DEPTH, "type": "Linear",
     "percent": 0.022},
    {"startVal": DIP_DEPTH, "endVal": 1.0, "type": "Linear",
     "percent": 0.030},
    {"startVal": 1.0, "endVal": 1.0, "type": "Linear", "percent": 0.0},
]

# Phrase hygiene for percent-mode envelopes (Matt's #4, same pass): a
# phrase IS one long note, so an unbounded percent stage stretches with
# the LINE — oboe1's 25%-of-duration release became a 3 s die-off that
# quashed line endings. maxSec clamps chosen to be INACTIVE at
# single-note lengths (<= 0.8 s here), so flat behavior is untouched
# and only the phrase-length stretch is pinned.
ENV_CLAMPS = {
    "oboe": {"Drive_env": {0: 0.35, 2: 0.25},
             "Ampl_env":  {0: 0.05, 2: 0.10}},
}

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

def teach(src_path, dst_path, breath_id, consumer_id, consumer_key,
          env_clamps=None):
    """Insert the tongue machinery; repoint consumer_id.params[consumer_key]
    from breath_id to a new multiply of (breath, TDip). env_clamps =
    {env_id: {stage_index: maxSec}} phrase-hygiene clamps."""
    p = json.load(open(src_path))
    p.pop("score", None)           # instrument patch, no smoke score
    p.setdefault("instrument", {})["transitions"] = ["tongue"]
    for env_id, clamps in (env_clamps or {}).items():
        node = next(n for n in p["graph"]["nodes"] if n["id"] == env_id)
        for idx, mx in clamps.items():
            node["params"]["stages"][idx]["maxSec"] = mx
    nodes = p["graph"]["nodes"]
    ids = {n["id"] for n in nodes}
    for nid in ("__perf_t", "NGt", "TDip", "BreathT"):
        assert nid not in ids, nid + " already exists in " + src_path
    # Plain refs resolve in file order (only taps may point forward), so
    # the new chain must sit BEFORE its consumer; the breath node it
    # multiplies is by construction earlier still.
    cons_idx = next(i for i, n in enumerate(nodes) if n["id"] == consumer_id)
    breath_idx = next(i for i, n in enumerate(nodes) if n["id"] == breath_id)
    assert breath_idx < cons_idx, (breath_id, consumer_id)
    nodes[cons_idx:cons_idx] = [
        {"id": "__perf_t", "type": "PerformNode",
         "params": {"field": "transition"}},
        {"id": "NGt", "type": "NameGate",
         "params": {"name": "tongue", "in": {"ref": "__perf_t"}}},
        {"id": "TDip", "type": "Envelope",
         "params": {"timeMode": "seconds", "stages": DIP_STAGES,
                    "trigger": {"ref": "NGt"}}},
        {"id": "BreathT", "type": "CombinedSource",
         "params": {"operation": 1, "gainAdj": 0.0,
                    "source1": {"ref": breath_id},
                    "source2": {"ref": "TDip"}}},
    ]
    by_id = {n["id"]: n for n in nodes}
    cons = by_id[consumer_id]
    ref = cons["params"][consumer_key]
    assert ref == {"ref": breath_id}, (consumer_id, consumer_key, ref)
    cons["params"][consumer_key] = {"ref": "BreathT"}
    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    json.dump(p, open(dst_path, "w"), indent=1)
    return dst_path

# ---------------------------------------------------------------------------
# scores
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
    ev, t = [], 0.0
    for midi, beats, start in seq:
        d = beats * Q
        e = {"note": float(midi), "velocity": 0.8, "duration": d}
        if phrased and not start:
            e["phrase"] = "cont"
        else:
            e["time"] = round(t, 6)
        ev.append(e)
        t += d
    return ev, t

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

def trough_count(x, sr, span_sec):
    """Inter-note silence proxy: 10 ms rms hop over the melody span;
    count dips below -25 dB relative to the running median."""
    hop = int(0.010 * sr)
    frames = [rms(x[i:i + hop]) for i in range(0, int(span_sec * sr), hop)]
    live = sorted(f for f in frames if f > 1e-5)
    if not live:
        return 0, 0.0
    med = live[len(live) // 2]
    thr = med * 10 ** (-25 / 20)
    count, inside, depth = 0, False, 0.0
    for f in frames:
        low = f < thr
        if low and not inside:
            count += 1
        if low:
            depth = max(depth, 20 * math.log10(max(f, 1e-9) / med))
        inside = low
    return count, depth

def main():
    os.makedirs(OUT, exist_ok=True)
    target = library_reference()
    print("presentation target (oboe1 sounding rms): %.4f" % target)

    taught = {
        "trombone": teach(SRC["trombone"],
                          os.path.join(PATCH_DIR, "trombone_tongue.json"),
                          "Mouth", "Pm", "source2"),
        "oboe": teach(SRC["oboe"],
                      os.path.join(PATCH_DIR, "oboe_tongue.json"),
                      "Drive_env", "Drive_vib", "frequency",
                      env_clamps=ENV_CLAMPS["oboe"]),
    }
    base = {"trombone": 48, "oboe": 60}       # house C4 / C5
    holdn = {"trombone": 46, "oboe": 64}      # A#3 / E5

    cells, stats = [], {}
    for inst, patch in taught.items():
        seq = otj(base[inst])
        span = sum(b for _, b, _ in seq) * Q
        for phrased in (False, True):
            tag = "otj_%s_%s" % (inst, "phrased" if phrased else "flat")
            x, sr, wp = render(patch, seq, phrased, tag)
            g = min(target / max(sounding_rms(x), 1e-9),
                    PEAK_CEIL / max(max(abs(s) for s in x), 1e-9))
            dst = os.path.join(OUT, tag + ".wav")
            write_mono_pair_scaled(wp, dst, g)
            tc, depth = trough_count(x, sr, span)
            stats[tag] = {"srms": sounding_rms(x), "gain": g,
                          "troughs": tc, "depth_db": depth}
            cells.append(tag)
            os.remove(wp)
        tag = "phrase_hold_%s" % inst
        x, sr, wp = render(patch, hold(holdn[inst]), True, tag)
        g = min(target / max(sounding_rms(x), 1e-9),
                PEAK_CEIL / max(max(abs(s) for s in x), 1e-9))
        write_mono_pair_scaled(wp, os.path.join(OUT, tag + ".wav"), g)
        tc, depth = trough_count(x, sr, 8 * Q)
        stats[tag] = {"srms": sounding_rms(x), "gain": g,
                      "troughs": tc, "depth_db": depth}
        cells.append(tag)
        os.remove(wp)

    # Machine gates, per instrument's physics. Oboe: its releases leave
    # real gaps, so phrasing must reduce inter-note troughs. Trombone:
    # kVoiceTail ring-out bridges the gaps in BOTH renders, so the gap
    # count is blind there — instead the tongue consonant must be
    # measurably present at in-phrase boundaries of the phrased render.
    f, p = stats["otj_oboe_flat"], stats["otj_oboe_phrased"]
    print("oboe   troughs: flat %d, phrased %d" % (f["troughs"],
                                                   p["troughs"]))
    assert p["troughs"] < f["troughs"], "oboe phrasing did not bite"

    px, psr = read_mono(os.path.join(OUT, "otj_trombone_phrased.wav"))
    ratios = []
    for k in (1, 2, 3, 5, 6, 8, 9, 10):        # in-phrase boundaries, line 1
        t = k * Q
        a = rms(px[int((t - 0.06) * psr):int((t - 0.01) * psr)])
        b = rms(px[int((t + 0.010) * psr):int((t + 0.045) * psr)])
        ratios.append(b / max(a, 1e-9))
    mean_dip = sum(ratios) / len(ratios)
    print("trombone tongue-dip ratios (post/pre boundary): " +
          " ".join("%.2f" % r for r in ratios))
    # Threshold matched to the 0.6-depth consonant (Matt's ear): the dent
    # is deliberately gentle now; this gate only proves it exists.
    assert mean_dip < 0.97, "trombone consonant not measurable"
    stats["tromb_dip"] = mean_dip

    readme = open(os.path.join(OUT, "README.md"), "w")
    readme.write(README_TMPL.format(
        oboe_flat=stats["otj_oboe_flat"]["troughs"],
        oboe_phr=stats["otj_oboe_phrased"]["troughs"],
        dip_pct=int(round((1.0 - stats["tromb_dip"]) * 100))))
    readme.close()
    print("cells:", ", ".join(cells))
    print("queue at", OUT)

README_TMPL = """# articulation1 - the first phrased renders

Ode to Joy, two lines. In the `flat` files every note is its own breath -
the instrument starts cold thirty times, which is what you have been
hearing all along. In the `phrased` files each LINE is one breath: the
first note starts the air, every later note is a tongue stroke on the
same moving air column, and the air stops at the end of the line.

## THE QUESTION

**Play `otj_trombone_flat.wav`, then `otj_trombone_phrased.wav`. Does the
phrased one sound like a player on one breath per line?** Then the same
pair for the oboe.

If you play only one file, play `otj_trombone_phrased.wav`.

`phrase_hold_trombone.wav` / `phrase_hold_oboe.wav` are the isolated
consonant: the same note eight times on one breath - all you should hear
between repeats is the tongue, never a restart.

Measured, so you know the mechanism is on. Oboe: counting near-silent
gaps between notes, the flat render has {oboe_flat} and the phrased one
{oboe_phr} - the air really is carrying through. Trombone: its ring-out
bridges the gaps in both renders, so listen to the attacks instead -
every flat note restarts cold, every phrased note is a tongue stroke
that dents the sound about {dip_pct}% and lets the bore ring on.

The tongue itself is one editable shape in each patch (node `TDip`: the
breath closes for 30 ms and reopens) - if the consonant reads too hard
or too soft, that envelope is the whole knob. The patches are
`patches/audition/articulation1/{{trombone,oboe}}_tongue.json`; the
originals they extend are untouched.
"""

if __name__ == "__main__":
    main()
