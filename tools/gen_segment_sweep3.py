"""gen_segment_sweep3.py — round 3: the combo family + pairs-of-combos.

From Matt's round-2 verdicts (ROUND1_VERDICTS.md): the twocomp impact —
wide body with a spike superimposed, ONE shape ("I'm all about economy") —
becomes its own family; twohit is replaced by PAIRS of combos. Lesson
applied: bare-pulse bodies live in ~8-40 ms (wider went sub-audio/rumble).

Output: patches/audition/segment_sweep3/{combo,combopair}/ +
        renders/dsp/pending/segment_sweep3/... via mforce_cli.
"""
import json, subprocess
from pathlib import Path
import gen_segment_sweep as g1
from gen_segment_sweep import Shape, atom, place, SR
from gen_segment_sweep2 import seq

ROOT = Path(__file__).resolve().parent.parent
CLI = ROOT / "build/tools/mforce_cli/Release/mforce_cli.exe"

def combo(body_w=0.025, body_shape="tri", body_pk=0.9, spike_w=0.0008,
          spike_pk=1.0, spike_at=0.0):
    """One impact: wide body + sharp spike superimposed (spike_at = fraction
    of body width into the body)."""
    return place([(0.0, atom(body_w, body_pk, body_shape)),
                  (body_w * spike_at, atom(spike_w, spike_pk, "sharp", 2.0))], tail=0)

COMBOS = [
    ("c08_tri",        dict(body_w=0.008)),
    ("c15_tri",        dict(body_w=0.015)),
    ("c25_tri",        dict(body_w=0.025)),
    ("c40_tri",        dict(body_w=0.040)),
    ("c25_sine",       dict(body_w=0.025, body_shape="sine")),
    ("c40_sine",       dict(body_w=0.040, body_shape="sine")),
    ("c25_in20",       dict(body_w=0.025, spike_at=0.2)),
    ("c25_in40",       dict(body_w=0.025, spike_at=0.4)),
    ("c25_spikelow",   dict(body_w=0.025, body_pk=1.0, spike_pk=0.65)),
    ("c25_bodylow",    dict(body_w=0.025, body_pk=0.6, spike_pk=1.0)),
    ("c15_fatspike",   dict(body_w=0.015, spike_w=0.002)),
    ("c40_fatspike_in30", dict(body_w=0.040, spike_w=0.002, spike_at=0.3)),
]

def pairs():
    c = lambda **k: combo(**k)
    return [
        ("heel_toe_g60",  seq(c(body_w=0.015), 0.06, c(body_w=0.010, body_pk=0.75, spike_pk=0.85))),
        ("heel_toe_g120", seq(c(body_w=0.015), 0.12, c(body_w=0.010, body_pk=0.75, spike_pk=0.85))),
        ("ka_thump_g80",  seq(c(body_w=0.008, body_pk=0.6, spike_pk=1.0), 0.08, c(body_w=0.035, body_shape="sine", body_pk=1.0, spike_pk=0.8))),
        ("ka_thump_g140", seq(c(body_w=0.008, body_pk=0.6, spike_pk=1.0), 0.14, c(body_w=0.035, body_shape="sine", body_pk=1.0, spike_pk=0.8))),
        ("thump_ka_g80",  seq(c(body_w=0.035, body_shape="sine", body_pk=1.0, spike_pk=0.8), 0.08, c(body_w=0.008, body_pk=0.6, spike_pk=1.0))),
        ("pair_equal_g100", seq(c(body_w=0.025), 0.10, c(body_w=0.025))),
        ("pair_fat_g150", seq(c(body_w=0.040, body_shape="sine"), 0.15, c(body_w=0.040, body_shape="sine", body_pk=0.7, spike_pk=0.8))),
        ("triple_run",    seq(c(body_w=0.025), 0.09, c(body_w=0.018, body_pk=0.8), 0.07, c(body_w=0.012, body_pk=0.65, spike_pk=0.8))),
    ]

def main():
    shapes = [Shape("combo", nm, "samples", place([(0, combo(**kw))]), **{k: str(v) for k, v in kw.items()})
              for nm, kw in COMBOS]
    shapes += [Shape("combopair", nm, "samples", buf) for nm, buf in pairs()]
    fails = 0
    for i, sh in enumerate(shapes):
        pd = ROOT / "patches/audition/segment_sweep3" / sh.family
        rd = ROOT / "renders/dsp/pending/segment_sweep3" / sh.family
        pd.mkdir(parents=True, exist_ok=True); rd.mkdir(parents=True, exist_ok=True)
        pp = pd / f"{sh.name}.json"
        pp.write_text(json.dumps(g1.patch_json(sh, 5000 + i), indent=1), encoding="utf-8")
        r = subprocess.run([str(CLI), str(pp), str(rd / f"{sh.name}.wav")], capture_output=True, text=True)
        if r.returncode != 0: fails += 1; print(sh.name, "FAIL")
    print(f"{len(shapes)} cells (combo {len(COMBOS)}, combopair 8); fails {fails}")

if __name__ == "__main__":
    main()
