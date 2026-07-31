"""Vowel-sequence baseline patches (Matt's directive 2026-07-30).

Baseline set of vowel sequences (OO-EE, O-OO-EE, AH-O-OO, LIAR, + two
invented) rendered at 220 Hz and 1320 Hz (LIAR also at 2640 Hz — the
"birdy" bet). Uses the new AdditiveSource formantFloor param (0.08) with
formantWeight 1.0 for real inter-formant suppression (~-22 dB valleys),
FullPartials 40 harmonics, 4 s notes.

ARCHITECTURE: gliding formants, not FormantSequence crossfade.
FormantSequence crossfades gains IN PLACE (band centers never move); at
f0 >= 1320 Hz no speech-table band (70-150 Hz wide) contains a harmonic
for ANY vowel, so a crossfade changes nothing — renders came out
byte-identical (verified). Instead each of the 3 formants has its
frequency/gain/width driven by an explicit-stage Envelope tracing the
vowel targets piecewise-linearly (a real diphthong glide). Gliding bands
sweep THROUGH the harmonics, producing the chirpy blooms at high f0 and
the same vowel endpoints at low f0.

Driver timing is a TRUE monotone ramp: hold first vowel to 0.4 s, glide
through the sequence reaching the last vowel at 3.5 s, hold. (The old
AREnvelope "ramp" rises then falls back — known bug; the preset-adsr
route clamps attack to 1.0 s via make_adsr attackMax.)

Writes patches/vowel_baseline/. Render + verify via run_vowel_baseline.py.
"""
import json
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "vowel_baseline")
os.makedirs(OUT, exist_ok=True)

SECONDS = 4
FMT_WEIGHT = 1.0
FMT_FLOOR = 0.08
N_HARM = 40

# (freq, gain, width, power) rows per vowel — same table as
# gen_formantseq_sweep.py, plus the LIAR schwa.
VOWEL = {
    "a":  [(730, 1.0, 90, 2.0), (1090, 0.7, 110, 2.0), (2440, 0.35, 140, 2.0)],
    "e":  [(530, 1.0, 90, 2.0), (1840, 0.6, 120, 2.0), (2480, 0.3, 140, 2.0)],
    "i":  [(270, 1.0, 70, 2.0), (2290, 0.6, 120, 2.0), (3010, 0.3, 150, 2.0)],
    "o":  [(570, 1.0, 90, 2.0), (840, 0.7, 100, 2.0), (2410, 0.3, 140, 2.0)],
    "u":  [(300, 1.0, 70, 2.0), (870, 0.6, 100, 2.0), (2240, 0.25, 140, 2.0)],
    "uh": [(500, 1.0, 100, 2.0), (1400, 0.6, 120, 2.0), (2500, 0.3, 140, 2.0)],
}

# name -> vowel-key sequence (first .. last)
SEQUENCES = {
    "oo_ee":    ["u", "i"],            # OO -> EE
    "o_oo_ee":  ["o", "u", "i"],       # O -> OO -> EE
    "ah_o_oo":  ["a", "o", "u"],       # AH -> O -> OO
    "liar":     ["a", "i", "uh"],      # (L)IAR: AH -> EE -> schwa
    "ee_ah_oo": ["i", "a", "u"],       # invented
    "oo_ah_ee": ["u", "a", "i"],       # invented
}

# (sequence, f0) render matrix: every sequence at 220 and 1320, liar also 2640.
RENDER_FREQS = {name: [220.0, 1320.0] for name in SEQUENCES}
RENDER_FREQS["liar"].append(2640.0)

# Glide timing (seconds of the 4 s note): hold first vowel, glide through
# the sequence (equal time per transition), hold last vowel. Hold ends at
# 0.7 s so the t=0.5 s verification window sits fully inside the pure
# first vowel.
RAMP_HOLD0 = 0.7
RAMP_END = 3.5


def glide_times(n_vowels):
    """[(t_start, t_end)] per transition, equal split of the glide window."""
    n_tr = n_vowels - 1
    seg = (RAMP_END - RAMP_HOLD0) / n_tr
    return [(RAMP_HOLD0 + i * seg, RAMP_HOLD0 + (i + 1) * seg)
            for i in range(n_tr)]


def param_track(vowel_keys, fmt_idx, param_idx):
    """Piecewise-linear (time, value) breakpoints for one formant param.

    param_idx: 0=frequency 1=gain 2=width. Track: hold v0 to RAMP_HOLD0,
    linear to each next vowel's value, arriving at the last by RAMP_END,
    hold to end of note.
    """
    vals = [VOWEL[vk][fmt_idx][param_idx] for vk in vowel_keys]
    pts = [(0.0, vals[0]), (RAMP_HOLD0, vals[0])]
    for (t0, t1), v in zip(glide_times(len(vals)), vals[1:]):
        pts.append((t1, v))
    pts.append((float(SECONDS), vals[-1]))
    return pts


def track_envelope_node(node_id, pts):
    """Explicit-stage Envelope tracing (time, value) breakpoints.

    Last stage is the percent-0 expand stage holding the final value, so
    rounding can never push the envelope past its stage list (which would
    output 0).
    """
    stages = []
    for (t0, v0), (t1, v1) in zip(pts[:-1], pts[1:]):
        if t1 <= t0:
            continue
        stages.append({"startVal": v0, "endVal": v1, "type": "Linear",
                       "percent": (t1 - t0) / SECONDS})
    # convert the final hold into the expand stage
    stages[-1]["percent"] = 0.0
    return {"id": node_id, "type": "Envelope", "params": {"stages": stages}}


def make_patch(vowel_keys, freq):
    nodes = [{"id": "ampEnv", "type": "Envelope",
              "params": {"preset": "adsr", "attack": 0.05, "decay": 0.05,
                         "sustainLevel": 0.8, "release": 0.1}}]
    fids = []
    n_formants = len(VOWEL[vowel_keys[0]])
    for fi in range(n_formants):
        f_env = track_envelope_node(f"f{fi}freq", param_track(vowel_keys, fi, 0))
        g_env = track_envelope_node(f"f{fi}gain", param_track(vowel_keys, fi, 1))
        w_env = track_envelope_node(f"f{fi}width", param_track(vowel_keys, fi, 2))
        nodes += [f_env, g_env, w_env]
        nodes.append({"id": f"fm{fi}", "type": "Formant",
                      "params": {"frequency": {"ref": f"f{fi}freq"},
                                 "gain": {"ref": f"f{fi}gain"},
                                 "width": {"ref": f"f{fi}width"},
                                 "power": 2.0}})
        fids.append({"ref": f"fm{fi}"})
    nodes.append({"id": "spec", "type": "FormantSpectrum",
                  "params": {"formants": fids}})

    nodes += [
        {"id": "fp", "type": "FullPartials",
         "params": {"maxPartials": N_HARM, "minMult": 1,
                    "rolloff1": 1.0, "rolloff2": 1.0}},
        {"id": "src", "type": "AdditiveSource",
         "params": {"seed": 42, "frequency": freq,
                    "amplitude": {"ref": "ampEnv"},
                    "formant": {"ref": "spec"},
                    "formantWeight": FMT_WEIGHT,
                    "formantFloor": FMT_FLOOR,
                    "partials": {"ref": "fp"}}},
        {"id": "ch1", "type": "SoundChannel", "inputs": {"source": "src"},
         "params": {"volume": 0.7, "pan": 0.0}},
        {"id": "mix", "type": "StereoMixer", "inputs": {"channels": ["ch1"]},
         "params": {"gainL": 1.0, "gainR": 1.0}},
    ]
    return {"sampleRate": 48000, "seconds": SECONDS,
            "graph": {"nodes": nodes, "output": "mix"}}


def render_list():
    """[(patch_name, sequence_name, f0)] for the full matrix."""
    out = []
    for name, freqs in RENDER_FREQS.items():
        for f in freqs:
            out.append((f"{name}_{int(f)}", name, f))
    return out


def main():
    n = 0
    for pname, sname, f in render_list():
        patch = make_patch(SEQUENCES[sname], f)
        with open(os.path.join(OUT, pname + ".json"), "w") as fp:
            json.dump(patch, fp, indent=1)
        n += 1
    print(f"wrote {n} vowel-baseline patches -> {OUT}")


if __name__ == "__main__":
    main()
