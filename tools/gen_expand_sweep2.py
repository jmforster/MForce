"""Expand-sweep round 2 (dsp item 5 cont.) — built on Matt's round-1 audition:

Musical family (Leslie-like: microcluster/wide_microcount/phase_swirl) gets
refined + hybridized; recursion is used explicitly (r2 = three expansion
levels); and spacing is now SWEPT — Matt's "attack that starts wide and
becomes narrow" — via multEnv (blends the spacing1-built m1 array toward
the spacing2-built m2 array), driven by fast-decay envelopes, full-length
ramps, and LFOs (cyclic wide/narrow "breathing", which should intersect
the Leslie family). Chordy family gets recursion + a chord<->Leslie morph.

patches/expand_sweep2/, render+rank via _run_expand_sweep2.py.
"""
import copy
import json
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "expand_sweep2")
os.makedirs(OUT, exist_ok=True)

FREQ, SECONDS = 220.0, 4

DEF = dict(count=3, recurse=0, spacing1=0.3, spacing2=0.3, dt1=0.02, dt2=0.02,
           loPct1=0.15, loPct2=0.15, power1=1.5, power2=1.5, po1=0.0, po2=0.0)

# (name, base_partials, er_overrides, driver)
# driver: None | ("adsr", decay) fast 1->0 | ("ramp",) 0->1 full length |
#         ("lfo", hz) cyclic 0..1. Drives multEnv: env=0 -> spacing1 side,
#         env=1 -> spacing2 side.
REGIMES = [
    # Leslie family refinement (round-1 Musical winners)
    ("leslie_micro_r2",    8, dict(count=2, recurse=2, spacing1=0.2, spacing2=0.2), None),
    ("leslie_wide8",       8, dict(count=8, recurse=0, spacing1=0.12, spacing2=0.12), None),
    ("leslie_swirl_combo", 8, dict(count=4, recurse=0, spacing1=0.18, spacing2=0.18,
                                   po1=0.5, po2=0.5), None),
    ("phase_swirl_r2",     6, dict(count=2, recurse=2, spacing1=0.5, spacing2=0.5,
                                   po1=0.5, po2=0.5), None),
    # Recursion depth probes
    ("micro_r2",           8, dict(count=2, recurse=2, spacing1=0.25, spacing2=0.25), None),
    ("semitone_r2",        6, dict(count=2, recurse=2, spacing1=1.0, spacing2=1.0), None),
    # Swept spacing
    ("attack_wide2narrow", 8, dict(count=3, recurse=1, spacing1=0.2, spacing2=2.5,
                                   dt1=0.0, dt2=0.04), ("adsr", 0.5)),
    ("attack_narrow2wide", 8, dict(count=3, recurse=1, spacing1=2.5, spacing2=0.2,
                                   dt1=0.04, dt2=0.0), ("adsr", 0.5)),
    ("sweep_slow_ramp",    8, dict(count=3, recurse=1, spacing1=0.15, spacing2=1.5), ("ramp",)),
    ("breathe_lfo_05",     8, dict(count=3, recurse=1, spacing1=0.2, spacing2=1.0), ("lfo", 0.5)),
    ("breathe_lfo_3",      8, dict(count=3, recurse=1, spacing1=0.2, spacing2=1.0), ("lfo", 3.0)),
    # Chordy family + morph
    ("fifth_r2",           6, dict(count=2, recurse=2, spacing1=7.0, spacing2=7.0), None),
    ("fifth_leslie_morph", 8, dict(count=3, recurse=1, spacing1=7.0, spacing2=0.2), ("lfo", 0.3)),
    # Rule-breaker
    ("*pi_swept",          8, dict(count=3, recurse=1, spacing1=3.1416, spacing2=0.1), ("adsr", 0.8)),
]


def driver_nodes(driver):
    if driver is None:
        return [], None
    if driver[0] == "adsr":
        return [{"id": "menv", "type": "Envelope",
                 "params": {"preset": "adsr", "attack": 0.005,
                            "decay": driver[1], "sustainLevel": 0.0,
                            "release": 0.0}}], "menv"
    if driver[0] == "ramp":
        return [{"id": "menv", "type": "AREnvelope",
                 "params": {"attack": 1.0, "attackMin": float(SECONDS),
                            "attackMax": float(SECONDS)}}], "menv"
    if driver[0] == "lfo":
        return [{"id": "mosc", "type": "SineSource",
                 "params": {"frequency": driver[1], "amplitude": 1.0}},
                {"id": "menv", "type": "RangeSource",
                 "params": {"min": 0.0, "max": 1.0, "normalized": False,
                            "var": {"ref": "mosc"}}}], "menv"
    raise ValueError(driver)


def make_patch(base_partials, er_over, driver):
    er = copy.deepcopy(DEF)
    er.update(er_over)
    dnodes, dref = driver_nodes(driver)
    fp_params = {"maxPartials": base_partials, "minMult": 1,
                 "evenWeight1": 1.0, "evenWeight2": 1.0,
                 "oddWeight1": 1.0, "oddWeight2": 1.0,
                 "rolloff1": 0.3, "rolloff2": 1.5,
                 "roEnv": {"ref": "roEnv"},
                 "expandRule": er}
    if dref:
        fp_params["multEnv"] = {"ref": dref}
    nodes = dnodes + [
        {"id": "ampEnv", "type": "Envelope",
         "params": {"preset": "adsr", "attack": 0.1, "decay": 0.1,
                    "sustainLevel": 0.7, "release": 0.0}},
        {"id": "roEnv", "type": "Envelope",
         "params": {"preset": "ar", "attack": 0.0, "attackMax": 0.0}},
        {"id": "fas_partials", "type": "FullPartials", "params": fp_params},
        {"id": "fas", "type": "AdditiveSource",
         "params": {"seed": 42, "frequency": FREQ,
                    "amplitude": {"ref": "ampEnv"},
                    "partials": {"ref": "fas_partials"}}},
        {"id": "ch1", "type": "SoundChannel", "inputs": {"source": "fas"},
         "params": {"volume": 0.7, "pan": 0.0}},
        {"id": "mix", "type": "StereoMixer", "inputs": {"channels": ["ch1"]},
         "params": {"gainL": 1.0, "gainR": 1.0}},
    ]
    return {"sampleRate": 48000, "seconds": SECONDS,
            "graph": {"nodes": nodes, "output": "mix"}}


def main():
    for name, bp, over, driver in REGIMES:
        fname = name.lstrip("*") + ".json"
        with open(os.path.join(OUT, fname), "w") as f:
            json.dump(make_patch(bp, over, driver), f, indent=1)
    print(f"wrote {len(REGIMES)} expand-sweep-2 patches -> {OUT}")


if __name__ == "__main__":
    main()
