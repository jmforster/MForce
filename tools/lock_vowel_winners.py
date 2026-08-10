"""Promote Matt-approved vowel patches into patches/library/voice/.

Matt's verdict, dsp/REVIEW.md item 23 (2026-08-09):
  "Diminishing returns. Only 1 improved on the previous winners, and that
   very marginally. Lock new Tenor_U_u1_f3peak."
  Happy to stick with prior _winners for Alto A/E/I/U, Bass U, Soprano A.
  Still unsatisfactory (NOT locked): Soprano E, Soprano I, Soprano U.

Copy is verbatim except for a `_provenance` block recording where the patch
came from and which verdict locked it. Run from the repo root.
"""
import json
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
DEST = ROOT / "patches" / "library" / "voice"

VERDICT = "Matt 2026-08-09, dsp/REVIEW.md item 23"

# locked name -> (source patch, why)
LOCKS = {
    "sing_alto_a": (
        "patches/vowel_tweak/Sing_Alto_A_v3_narrow.json",
        "pass-1 winner; pass-2 w1/w2/w3 all sounded identical to it",
    ),
    "sing_alto_e": (
        "patches/vowel_tweak/Sing_Alto_E_v1_open.json",
        "pass-1 winner; pass-2 blur variants added wobble",
    ),
    "sing_alto_i": (
        "patches/vowel_tweak/Sing_Alto_I_v2_f2up.json",
        "pass-1 winner; pass-2 no improvement",
    ),
    "sing_alto_u": (
        "patches/vowel_tweak/Sing_Alto_U_v1_speechUW.json",
        "pass-1 winner; pass-2 no improvement",
    ),
    "sing_bass_u": (
        "patches/vowel_tweak/Sing_Bass_U_v3_midF2.json",
        "pass-1 winner; pass-2 UW-top grafts did not beat it",
    ),
    "sing_soprano_a": (
        "patches/vowel_tweak/Sing_Soprano_A_v1_wide.json",
        "pass-1 winner; pass-2 ring-mitigation ladder no improvement",
    ),
    "sing_tenor_u": (
        "patches/vowel_tweak2/Sing_Tenor_U_u1_f3peak.json",
        "pass-2 NEW winner - UW F3 peak (2355 Hz) grafted onto the pass-1 "
        "winner; the only pass-2 variant Matt preferred",
    ),
}


def main():
    DEST.mkdir(parents=True, exist_ok=True)
    written = []
    for name, (src_rel, why) in LOCKS.items():
        src = ROOT / src_rel
        if not src.exists():
            print(f"MISSING {src_rel}", file=sys.stderr)
            return 1
        patch = json.loads(src.read_text())
        patch["_provenance"] = {
            "source": src_rel,
            "locked_by": VERDICT,
            "why": why,
        }
        out = DEST / f"{name}.json"
        out.write_text(json.dumps(patch, indent=2) + "\n")
        written.append((name, src_rel))
        print(f"{name:16s} <- {src_rel}")
    print(f"\n{len(written)} patches locked into {DEST.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
