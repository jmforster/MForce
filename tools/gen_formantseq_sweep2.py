"""FormantSequence round 2 — weight cranked (Matt's round-1 verdict:
"All nearly identical, subtle — I'm guessing FormantWeight was too low?").

Mechanically right: fmtWt 2 boosts in-band partials by up to 3x but the
untouched out-of-band partials dominate the mix. Round 2 re-runs a focused
set of round-1 regimes at formantWeight 5 and 9, plus one flattened-source
variant (rolloff 0.4 instead of 1.0, so the formants shape most of what
you hear) and a static-blend control at the new weight.

Reuses gen_formantseq_sweep's template/driver machinery by import.
Writes patches/formantseq_sweep2/. Render via _run_formantseq_sweep2.py.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_formantseq_sweep import AIU, V5, NARROW, COMB, make_patch  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "formantseq_sweep2")
os.makedirs(OUT, exist_ok=True)

# (name, spectra, driver, formantWeight, static_blend_or_None, flat_rolloff)
REGIMES = [
    ("vowel_glide_ramp_w5",  AIU,    "ramp",  5.0, None, None),
    ("vowel_glide_ramp_w9",  AIU,    "ramp",  9.0, None, None),
    ("vowel_lfo_1hz_w5",     AIU,    "lfo1",  5.0, None, None),
    ("vowel_lfo_1hz_w9",     AIU,    "lfo1",  9.0, None, None),
    ("vowel5_red_walk_w5",   V5,     "red",   5.0, None, None),
    ("vowel5_red_walk_w9",   V5,     "red",   9.0, None, None),
    ("audio_rate_30_w5",     COMB,   "lfo30", 5.0, None, None),
    ("audio_rate_30_w9",     COMB,   "lfo30", 9.0, None, None),
    ("narrow_audio_50_w5",   NARROW, "lfo50", 5.0, None, None),
    ("narrow_audio_50_w9",   NARROW, "lfo50", 9.0, None, None),
    # Flatter source (round-1 rolloff was 1.0) so the formants dominate
    ("vowel_glide_flat",     AIU,    "ramp",  5.0, None, 0.4),
    # New reference at the cranked weight
    ("control_static_w5",    AIU,    None,    5.0, 0.0,  None),
]


def main():
    for name, spectra, driver, wt, blend, flat in REGIMES:
        patch = make_patch(spectra, driver, wt, blend)
        if flat is not None:
            fp = next(n for n in patch["graph"]["nodes"] if n["id"] == "fp")
            fp["params"]["rolloff1"] = flat
            fp["params"]["rolloff2"] = flat
        with open(os.path.join(OUT, name + ".json"), "w") as f:
            json.dump(patch, f, indent=1)
    print(f"wrote {len(REGIMES)} formantseq-sweep-2 patches -> {OUT}")


if __name__ == "__main__":
    main()
