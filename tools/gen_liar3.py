"""LIAR v3 — exact reconstruction from the legacy scene YAML
(docs/Formant - Liar.unity, decoded 2026-07-31).

True legacy architecture: THREE formants whose FREQUENCIES glide through
4-stage envelopes (asynchronous per formant — real coarticulation), over a
FullPartials buzz with rolloff 1 (not flat), spectrum weight 1 (= floor 0).
Trajectories are Peterson-Barney /O/ -> /i/ -> /3r/ with an L onset:
  F1: 70 ->570(15%) ->270(+30%) ->490(+30%) hold
  F2: 1500->840(10%) ->2290(+30%) ->1350(+30%) hold
  F3: 1700->2410(10%) ->3010(+40%) ->1690(+40%) hold
Gains 1.0/0.5/0.2, power 1. Scene widths are 8/5/3 in an unknown unit
(the wrapper that converted them is lost). Two interpretations rendered:
  hz:   literal 8/5/3 Hz -> near-pure gliding tones (sine-wave speech)
  x100: 800/500/300 Hz  -> wide-band voiced version
Matt's ear is the arbiter of which matches the remembered scene.
"""
import json
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "liar3")
os.makedirs(OUT, exist_ok=True)

SECONDS = 3.0

# (segments as (endVal, percent)) with startVal chaining; last stage holds.
F_TRAJ = {
    "F1": (70,   [(570, 0.15), (270, 0.30), (490, 0.30), (480, 0.0)], 1.0),
    "F2": (1500, [(840, 0.10), (2290, 0.30), (1350, 0.30), (1330, 0.0)], 0.5),
    "F3": (1700, [(2410, 0.10), (3010, 0.40), (1690, 0.40), (1670, 0.0)], 0.2),
}
WIDTHS = {"hz": {"F1": 8, "F2": 5, "F3": 3},
          "x100": {"F1": 800, "F2": 500, "F3": 300}}
F0S = [110, 880]


def stage_env(env_id, start, segs):
    stages, v = [], start
    for end, pct in segs:
        stages.append({"startVal": v, "endVal": end, "type": "Linear",
                       "percent": pct})
        v = end
    stages[-1]["percent"] = 0.0
    return {"id": env_id, "type": "Envelope", "params": {"stages": stages}}


def make_patch(f0, wkey):
    # Scene amp env (MAEStg1-4): fast attack (<=0.08s), 1->0.8, 0.8->0.4, ->0
    amp = {"id": "ampEnv", "type": "Envelope", "params": {"stages": [
        {"startVal": 0.0, "endVal": 1.0, "type": "Linear", "percent": 0.027},
        {"startVal": 1.0, "endVal": 0.8, "type": "Linear", "percent": 0.2},
        {"startVal": 0.8, "endVal": 0.4, "type": "Linear", "percent": 0.0},
        {"startVal": 0.4, "endVal": 0.0, "type": "Sine", "percent": 0.1},
    ]}}
    nodes = [amp]
    fids = []
    for name, (start, segs, gain) in F_TRAJ.items():
        eid = f"{name}_freq"
        nodes.append(stage_env(eid, start, segs))
        nodes.append({"id": name, "type": "Formant",
                      "params": {"frequency": {"ref": eid}, "gain": gain,
                                 "width": WIDTHS[wkey][name], "power": 1.0}})
        fids.append({"ref": name})
    nodes += [
        {"id": "spec", "type": "FormantSpectrum", "params": {"formants": fids}},
        {"id": "fp", "type": "FullPartials",
         "params": {"maxPartials": 160, "minMult": 1,
                    "rolloff1": 1.0, "rolloff2": 1.0}},
        {"id": "src", "type": "AdditiveSource",
         "params": {"seed": 42, "frequency": float(f0),
                    "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "spec"},
                    "formantWeight": 1.0, "formantFloor": 0.0,
                    "partials": {"ref": "fp"}}},
        {"id": "ch1", "type": "SoundChannel", "inputs": {"source": "src"},
         "params": {"volume": 0.8, "pan": 0.0}},
        {"id": "mix", "type": "StereoMixer", "inputs": {"channels": ["ch1"]},
         "params": {"gainL": 1.0, "gainR": 1.0}},
    ]
    return {"sampleRate": 48000, "seconds": SECONDS,
            "graph": {"nodes": nodes, "output": "mix"}}


def main():
    for wkey in WIDTHS:
        for f0 in F0S:
            path = os.path.join(OUT, f"liar3_{wkey}_{f0}.json")
            with open(path, "w") as f:
                json.dump(make_patch(f0, wkey), f, indent=1)
            print("wrote", path)


if __name__ == "__main__":
    main()
