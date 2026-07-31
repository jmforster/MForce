"""Build the clarinet CMA-ES template patch (patches/clarinet_c1/template.json).

Warm-start structure per the clarinet attack report (out/clarinet_attack_report.md)
and the noise-bed feature (AdditiveSource noiseBed* configs):

- ExplicitPartials baseline ampl = measured clarinet harmonic envelope
  (mean dB across the eval notes E3/D4/F4 from out/clarinet_reference.json,
  harmonics 1-32; 33-40 extrapolated on the tail slope). The CMA-ES env
  knots multiply this baseline.
- No Vibrato node (clarinet: vibrato pinned OFF).
- No formant/BandSpectrum (spectral shape lives in the ampl baseline).
- FAST amp envelope: the noise bed's level tracks the amplitude param, so
  a slow ampEnv attack would delay the bed too. The tone's slow rise is
  carried instead by noiseBedDelay (bed leads) + noiseBedFade (t50 rise),
  both drivable per register via paramMap frequency curves.

Run: python build_clarinet_template.py
"""
import json
import os

import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
REF = os.path.join(HERE, "out", "clarinet_reference.json")
OUT_DIR = os.path.join(REPO, "patches", "clarinet_c1")
OUT = os.path.join(OUT_DIR, "template.json")

N_PARTIALS = 40
EVAL_NOTES = ["E3", "D4", "F4"]


def baseline_ampl():
    ref = json.load(open(REF))
    envs = [np.array(ref["notes"][n]["harm_env_db"]) for n in EVAL_NOTES]
    mean_db = np.mean(envs, axis=0)          # 32 harmonics
    # extrapolate 33..N on the mean slope of the last 8 harmonics
    tail_slope = np.polyfit(np.arange(24, 32), mean_db[24:32], 1)[0]
    ext = mean_db[-1] + tail_slope * np.arange(1, N_PARTIALS - len(mean_db) + 1)
    db = np.concatenate([mean_db, ext])
    amp = 10 ** (db / 20.0)
    return (amp / amp.max()).round(6).tolist()


def main():
    os.makedirs(OUT_DIR, exist_ok=True)
    ampl = baseline_ampl()
    mult = [float(k) for k in range(1, N_PARTIALS + 1)]
    patch = {
        "sampleRate": 48000,
        "graph": {
            "nodes": [
                {"id": "ampEnv", "type": "Envelope",
                 "params": {"preset": "adsr", "attack": 0.01, "decay": 0.1,
                            "sustainLevel": 1.0, "release": 0.1}},
                {"id": "clr_partials", "type": "ExplicitPartials",
                 "params": {
                     "mult1": mult, "mult2": mult,
                     "ampl1": ampl, "ampl2": ampl,
                     "rolloff1": 0.0, "rolloff2": 0.0,
                     "detune1": 0.0, "detune2": 0.0}},
                {"id": "clr", "type": "AdditiveSource",
                 "params": {
                     "seed": 7,
                     "frequency": 293.66,
                     "amplitude": {"ref": "ampEnv"},
                     "partials": {"ref": "clr_partials"},
                     "noiseBedLevel": 0.016,
                     "noiseBedFreq": 2900.0,
                     "noiseBedWidth": 1500.0,
                     "noiseBedDelay": 0.1,
                     "noiseBedFade": 0.2}},
            ],
            "output": "clr",
        },
        "instrument": {"polyphony": 1,
                       "paramMap": {"frequency": ["clr.frequency"]}},
    }
    json.dump(patch, open(OUT, "w"), indent=1)
    print(f"wrote {OUT}  ({N_PARTIALS} partials, baseline from {EVAL_NOTES})")


if __name__ == "__main__":
    main()
