"""FormantSequence deep-dive sweep (dsp BACKLOG item 6, G2).

Sequenced/modulated formant motion as a first-class timbre animator: a
common FullPartials base (220 Hz, 4 s) whose AdditiveSource formant input
is a FormantSequence over 2-5 FormantSpectrum nodes, with `blend` driven
by ramps, LFOs, RedNoise/Perlin walkers — plus audio-rate rule-breakers
(blend oscillating at 30-220 Hz: spectral-envelope modulation fast enough
to grow sidebands; unexplored territory).

Writes patches/formantseq_sweep/. Render + novelty-rank via
_run_formantseq_sweep.py (same pipeline as the expand sweep).
"""
import json
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "formantseq_sweep")
os.makedirs(OUT, exist_ok=True)

FREQ, SECONDS = 220.0, 4

# (freq, gain, width, power) rows per spectrum.
VOWEL = {
    "a": [(730, 1.0, 90, 2.0), (1090, 0.7, 110, 2.0), (2440, 0.35, 140, 2.0)],
    "e": [(530, 1.0, 90, 2.0), (1840, 0.6, 120, 2.0), (2480, 0.3, 140, 2.0)],
    "i": [(270, 1.0, 70, 2.0), (2290, 0.6, 120, 2.0), (3010, 0.3, 150, 2.0)],
    "o": [(570, 1.0, 90, 2.0), (840, 0.7, 100, 2.0), (2410, 0.3, 140, 2.0)],
    "u": [(300, 1.0, 70, 2.0), (870, 0.6, 100, 2.0), (2240, 0.25, 140, 2.0)],
}
AIU = [VOWEL["a"], VOWEL["i"], VOWEL["u"]]
V5 = [VOWEL[v] for v in "aeiou"]
SWEEP = [[(300, 1.0, 220, 3.0)], [(3000, 1.0, 350, 3.0)]]  # widths must straddle the 220 Hz harmonic grid
NARROW = [[(500, 1.0, 160, 3.0)], [(1200, 1.0, 200, 3.0)], [(2600, 1.0, 260, 3.0)]]
COMB_A = [[(f, 1.0, 60, 2.5) for f in range(400, 3400, 500)]]
COMB_B = [[(f + 250, 1.0, 60, 2.5) for f in range(400, 3400, 500)]]
COMB = [COMB_A[0], COMB_B[0]]
INHARM = [[(800, 1.0, 70, 2.5), (1900, 0.8, 90, 2.5), (2700, 0.5, 110, 2.5)],
          [(500, 1.0, 70, 2.5), (1400, 0.8, 90, 2.5), (3300, 0.5, 110, 2.5)]]


def driver_nodes(kind):
    """Return (nodes, blend_ref) for a 0..1 blend driver."""
    if kind == "ramp":
        return ([{"id": "drv", "type": "AREnvelope",
                  "params": {"attack": 1.0, "attackMin": float(SECONDS),
                             "attackMax": float(SECONDS)}}], "drv")
    if kind.startswith("lfo"):
        hz = float(kind[3:])
        return ([{"id": "osc", "type": "SineSource",
                  "params": {"frequency": hz, "amplitude": 1.0}},
                 {"id": "drv", "type": "RangeSource",
                  "params": {"min": 0.0, "max": 1.0, "normalized": False,
                             "var": {"ref": "osc"}}}], "drv")
    if kind == "red":
        return ([{"id": "osc", "type": "RedNoiseSource", "params": {}},
                 {"id": "drv", "type": "RangeSource",
                  "params": {"min": 0.0, "max": 1.0, "normalized": False,
                             "var": {"ref": "osc"}}}], "drv")
    if kind == "perlin":
        return ([{"id": "osc", "type": "PerlinNoiseSource", "params": {}},
                 {"id": "drv", "type": "RangeSource",
                  "params": {"min": 0.0, "max": 1.0, "normalized": False,
                             "var": {"ref": "osc"}}}], "drv")
    raise ValueError(kind)


# (name, spectra, driver, formantWeight, static_blend_or_None)
REGIMES = [
    ("control_static",    AIU,    None,      2.0, 0.0),
    ("vowel_glide_ramp",  AIU,    "ramp",    2.0, None),
    ("vowel_lfo_1hz",     AIU,    "lfo1",    2.0, None),
    ("vowel_lfo_4hz",     AIU,    "lfo4",    2.0, None),
    ("vowel5_red_walk",   V5,     "red",     2.0, None),
    ("vowel_perlin",      AIU,    "perlin",  2.0, None),
    ("sweep_lohi_ramp",   SWEEP,  "ramp",    2.5, None),
    ("narrow_glide_lfo03", NARROW, "lfo0.3", 3.0, None),
    ("comb_morph_lfo1",   COMB,   "lfo1",    2.0, None),
    ("inharm_morph_red",  INHARM, "red",     2.0, None),
    # --- rule-breakers: audio-rate spectral-envelope modulation ---
    ("*audio_rate_30",    COMB,   "lfo30",   2.0, None),
    ("*audio_rate_70",    AIU,    "lfo70",   2.0, None),
    ("*audio_rate_220",   AIU,    "lfo220",  2.0, None),
    ("*deep_weight_10",   AIU,    "lfo1",    10.0, None),
    ("*narrow_audio_50",  NARROW, "lfo50",   3.0, None),
]


def make_patch(spectra, driver, fmt_wt, static_blend):
    nodes = [{"id": "ampEnv", "type": "Envelope",
              "params": {"preset": "adsr", "attack": 0.1, "decay": 0.1,
                         "sustainLevel": 0.7, "release": 0.0}}]
    spec_ids = []
    for si, rows in enumerate(spectra):
        fids = []
        for fi, (fr, g, w, pw) in enumerate(rows):
            fid = f"s{si}f{fi}"
            nodes.append({"id": fid, "type": "Formant",
                          "params": {"frequency": fr, "gain": g,
                                     "width": w, "power": pw}})
            fids.append({"ref": fid})
        sid = f"spec{si}"
        nodes.append({"id": sid, "type": "FormantSpectrum",
                      "params": {"formants": fids}})
        spec_ids.append({"ref": sid})

    fseq_params = {"spectra": spec_ids}
    if driver is None:
        fseq_params["blend"] = static_blend
    else:
        dnodes, dref = driver_nodes(driver)
        nodes = dnodes + nodes
        fseq_params["blend"] = {"ref": dref}
    nodes.append({"id": "fseq", "type": "FormantSequence",
                  "params": fseq_params})

    nodes += [
        {"id": "fp", "type": "FullPartials",
         "params": {"maxPartials": 40, "minMult": 1, "rolloff1": 1.0,
                    "rolloff2": 1.0}},
        {"id": "src", "type": "AdditiveSource",
         "params": {"seed": 42, "frequency": FREQ,
                    "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "fseq"}, "formantWeight": fmt_wt,
                    "partials": {"ref": "fp"}}},
        {"id": "ch1", "type": "SoundChannel", "inputs": {"source": "src"},
         "params": {"volume": 0.7, "pan": 0.0}},
        {"id": "mix", "type": "StereoMixer", "inputs": {"channels": ["ch1"]},
         "params": {"gainL": 1.0, "gainR": 1.0}},
    ]
    return {"sampleRate": 48000, "seconds": SECONDS,
            "graph": {"nodes": nodes, "output": "mix"}}


def main():
    for name, spectra, driver, wt, blend in REGIMES:
        fname = name.lstrip("*") + ".json"
        with open(os.path.join(OUT, fname), "w") as f:
            json.dump(make_patch(spectra, driver, wt, blend), f, indent=1)
    print(f"wrote {len(REGIMES)} formantseq-sweep patches -> {OUT}")


if __name__ == "__main__":
    main()
