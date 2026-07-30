"""Expand-sweep round 3 — built on Matt's round-2 audition verdicts.

Reuses gen_expand_sweep2's template/driver machinery by import.

- fifth_r2_p10: fifth_r2 with base partials pushed to 10 (~1250 partials).
- swirl_r1/swirl_r2: leslie_swirl_combo (count=4, spacing 0.18, po 0.5)
  with recursion. r2 at count 4 = 9^3 = 729x per base partial, so base
  partials drop to 4 (2916 total); r1 keeps base 8.
- wide2narrow_002/_005/_008: attack_wide2narrow with the wide->narrow
  transition compressed to 0.02 / 0.05 / 0.08 s ("could be a useful musical
  attack effect, if much faster"). Envelope decay is a FRACTION of the 4 s
  note, so 0.02 s = decay 0.005, 0.05 s = 0.0125, 0.08 s = 0.02.
- morph_quick: chord collapsing to Leslie in ~0.2 s then STAYING Leslie.
  adsr driver (attack 0.005, decay 0.05, sustain 0) ends at env=0 =
  spacing1, so the spacings are swapped vs. fifth_leslie_morph:
  spacing1=0.2 (Leslie, final/held) and spacing2=7.0 (chord, initial).
- morph_audio_30/_110: fifth_leslie_morph with the blend LFO at audio
  rates 30 Hz and 110 Hz.

patches/expand_sweep3/, render+rank via _run_expand_sweep3.py.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from gen_expand_sweep2 import make_patch  # noqa: E402

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "expand_sweep3")
os.makedirs(OUT, exist_ok=True)

# (name, base_partials, er_overrides, driver) — same shape as round 2.
REGIMES = [
    # Chordy recursion, base partials pushed to 10 (~1250 partials, slow render)
    ("fifth_r2_p10",    10, dict(count=2, recurse=2, spacing1=7.0, spacing2=7.0), None),
    # Swirl + recursion
    ("swirl_r1",         8, dict(count=4, recurse=1, spacing1=0.18, spacing2=0.18,
                                 po1=0.5, po2=0.5), None),
    ("swirl_r2",         4, dict(count=4, recurse=2, spacing1=0.18, spacing2=0.18,
                                 po1=0.5, po2=0.5), None),
    # Fast wide->narrow attacks (transition seconds in the name)
    ("wide2narrow_002",  8, dict(count=3, recurse=1, spacing1=0.2, spacing2=2.5,
                                 dt1=0.0, dt2=0.04), ("adsr", 0.005)),
    ("wide2narrow_005",  8, dict(count=3, recurse=1, spacing1=0.2, spacing2=2.5,
                                 dt1=0.0, dt2=0.04), ("adsr", 0.0125)),
    ("wide2narrow_008",  8, dict(count=3, recurse=1, spacing1=0.2, spacing2=2.5,
                                 dt1=0.0, dt2=0.04), ("adsr", 0.02)),
    # Quick chord->Leslie morph that stays Leslie (spacings swapped, see above)
    ("morph_quick",      8, dict(count=3, recurse=1, spacing1=0.2, spacing2=7.0), ("adsr", 0.05)),
    # Audio-rate chord<->Leslie morphing
    ("morph_audio_30",   8, dict(count=3, recurse=1, spacing1=7.0, spacing2=0.2), ("lfo", 30.0)),
    ("morph_audio_110",  8, dict(count=3, recurse=1, spacing1=7.0, spacing2=0.2), ("lfo", 110.0)),
]


def main():
    for name, bp, over, driver in REGIMES:
        with open(os.path.join(OUT, name + ".json"), "w") as f:
            json.dump(make_patch(bp, over, driver), f, indent=1)
    print(f"wrote {len(REGIMES)} expand-sweep-3 patches -> {OUT}")


if __name__ == "__main__":
    main()
