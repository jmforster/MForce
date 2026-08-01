"""LIAR v2 — legacy-faithful reconstruction (Matt, 2026-07-31).

Signal path = the legacy "Formant - Liar" unity scene as Matt describes it:
flat BUZZ source (FullPartials, NO rolloff) -> FormantSequence crossfading
through 5-formant spectra -> formantWeight 1.0 + formantFloor 0.0 (the
exact algebraic equivalent of legacy crossfade wt=1: out-of-band silent).

Word: L -> AH -> (diphthong glide) -> EE -> ER, as 4 spectra on the
sequence; blend driven by an explicit-stage envelope (preset adsr clamps
its attack at 1s — known issue — so stages are explicit):
  hold L, glide to AH, hold AH, long glide AH->EE (the /aI/), glide
  EE->ER, hold ER.

5-formant tables (male-voice values from the classic published formant
tables — the same family as the UChicago phonetics pages; L and R from
acoustic-phonetics literature; ER's low F3 ~1690 Hz IS the R-color):
renders at f0 110 (speech), 220, 660, 1320 (the chirp test).
"""
import json
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "liar2")
os.makedirs(OUT, exist_ok=True)

SECONDS = 3.0

# (freq, gain, width) x5 per phone; power 2.0 everywhere.
# WIDTH SEMANTICS (bug fixed 2026-07-31): engine width = FULL band footprint
# (center +/- width/2), legacy default 500 — NOT the literature's -3dB
# resonance bandwidth (40-160 Hz). Literature widths admit only 1-2 harmonics
# per formant at speech f0 -> isolated partials -> Matt heard a chime.
# Engine-appropriate widths below span many harmonics each; overlapping bands
# sum into a continuous voiced envelope (crossfade skirts come from the
# power-2 taper across the wide band).
PHONES = {
    "L":  [(360, 0.70, 420), (1300, 0.25, 600), (2700, 0.12, 800),
           (3300, 0.05, 900), (3700, 0.02, 1000)],
    "AH": [(650, 1.00, 500), (1080, 0.50, 600), (2650, 0.35, 800),
           (2900, 0.40, 900), (3250, 0.10, 1000)],
    "EE": [(290, 1.00, 350), (1870, 0.18, 650), (2800, 0.12, 800),
           (3250, 0.10, 900), (3540, 0.03, 1000)],
    "ER": [(490, 1.00, 450), (1350, 0.35, 600), (1690, 0.30, 650),
           (3300, 0.06, 900), (3600, 0.03, 1000)],
}
SEQ = ["L", "AH", "EE", "ER"]

# Blend timeline (seconds, blend value at 4 spectra positions 0/.333/.667/1):
# hold L, L->AH, hold AH, AH->EE diphthong, EE->ER, hold ER.
TIMELINE = [(0.00, 0.0), (0.15, 0.0), (0.35, 1/3), (0.90, 1/3),
            (1.70, 2/3), (2.30, 1.0), (SECONDS, 1.0)]

F0S = [110, 220, 660, 1320]


def blend_env_node():
    stages = []
    for (t0, v0), (t1, v1) in zip(TIMELINE, TIMELINE[1:]):
        stages.append({"startVal": v0, "endVal": v1, "type": "Linear",
                       "percent": (t1 - t0) / SECONDS})
    stages[-1]["percent"] = 0.0  # final stage absorbs remainder
    return {"id": "blendEnv", "type": "Envelope", "params": {"stages": stages}}


def make_patch(f0, floor=0.0, wscale=1.0):
    nodes = [blend_env_node(),
             {"id": "ampEnv", "type": "Envelope",
              "params": {"preset": "adsr", "attack": 0.03, "decay": 0.05,
                         "sustainLevel": 0.9, "release": 0.0}}]
    spec_refs = []
    for phone in SEQ:
        fids = []
        for i, (fr, g, w) in enumerate(PHONES[phone]):
            fid = f"{phone}_f{i}"
            nodes.append({"id": fid, "type": "Formant",
                          "params": {"frequency": fr, "gain": g,
                                     "width": w * wscale, "power": 2.0}})
            fids.append({"ref": fid})
        sid = f"spec_{phone}"
        nodes.append({"id": sid, "type": "FormantSpectrum",
                      "params": {"formants": fids}})
        spec_refs.append({"ref": sid})

    nodes += [
        {"id": "fseq", "type": "FormantSequence",
         "params": {"spectra": spec_refs, "blend": {"ref": "blendEnv"}}},
        # The legacy buzz: full partials, NO rolloff (flat harmonics).
        {"id": "fp", "type": "FullPartials",
         "params": {"maxPartials": 160, "minMult": 1,
                    "rolloff1": 0.0, "rolloff2": 0.0}},
        {"id": "src", "type": "AdditiveSource",
         "params": {"seed": 42, "frequency": float(f0),
                    "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "fseq"},
                    "formantWeight": 1.0, "formantFloor": floor,
                    "partials": {"ref": "fp"}}},
    ]
    # Instrument-style (UI keyboard/Generate + CLI render both work): output
    # is the source node; blend/amp envelope percents scale to note length.
    import math
    midi = int(round(69 + 12 * math.log2(f0 / 440.0)))
    return {"sampleRate": 48000,
            "graph": {"nodes": nodes, "output": "src"},
            "instrument": {"paramMap": {"frequency": "src.frequency"},
                           "polyphony": 1},
            "score": [{"note": midi, "velocity": 0.85, "time": 0.0,
                       "duration": SECONDS}]}


def main():
    for f0 in F0S:
        path = os.path.join(OUT, f"liar2_{f0}.json")
        with open(path, "w") as f:
            json.dump(make_patch(f0), f, indent=1)
        print("wrote", path)
    # Width-vs-floor ladder (Matt 2026-07-31: floor and wide bands are
    # substitutes for filling valleys — with a floor, bands can narrow).
    # med = half-width bands + small floor; narrow = 0.3x + floor 0.05.
    for f0 in (110, 220):
        for tag, fl, ws in (("fl05", 0.05, 1.0),
                            ("med_fl03", 0.03, 0.5),
                            ("narrow_fl05", 0.05, 0.3)):
            path = os.path.join(OUT, f"liar2_{f0}_{tag}.json")
            with open(path, "w") as f:
                json.dump(make_patch(f0, floor=fl, wscale=ws), f, indent=1)
            print("wrote", path)


if __name__ == "__main__":
    main()
