"""Generate viola patches with physics-justified partials + literature body
resonances.

Source spectrum: 1/n (sawtooth — exact spectrum of an ideal bowed-string
stick-slip excitation). Source spectrum is then shaped by Formant nodes
modeling the viola body's signature resonances.

Body resonances (viola = violin shifted ~7 semitones lower because the body
is ~10% larger):
  - A0   (Helmholtz/air mode):       ~220 Hz   (violin A0 = ~280)
  - B1-  (body bending mode):        ~440 Hz
  - B1+  (body twin mode):           ~510 Hz
  - C-bout / mid-body:               ~660-1200 Hz
  - Bridge hill (the famous peak):   ~2100 Hz  (violin = ~2300)
  - Super-bridge / high formant:     ~3500 Hz

Source: signature-modes literature (Euphonics §5.3, Fletcher & Rossing
"Physics of Musical Instruments" Ch. 10).

Variants sweep partial count + spectral envelope + bridge hill strength.
"""
import json
import subprocess
import sys
from pathlib import Path

REPO = Path("C:/@dev/repos/mforce")
CLI = REPO / "build/tools/mforce_cli/Release/mforce_cli.exe"
PATCHES_DIR = REPO / "patches/viola"
RENDERS_BASE = REPO / "renders/explore"

SECONDS = 5.0
FUNDAMENTAL = 220.0           # open A string


# Viola body resonance formants — (id, freq, gain, width)
BODY_FULL = [
    ("a0",         220.0, 0.9,  60.0),
    ("b1_minus",   440.0, 0.7, 100.0),
    ("b1_plus",    510.0, 0.6, 110.0),
    ("cbout",      660.0, 0.55, 180.0),
    ("midbody",   1200.0, 0.45, 400.0),
    ("bridge",    2100.0, 0.85, 700.0),
    ("superbridge", 3500.0, 0.4, 900.0),
]

BODY_MINIMAL = [
    ("a0",         220.0, 0.9,  60.0),
    ("cbout",      660.0, 0.55, 250.0),
    ("bridge",    2100.0, 0.85, 800.0),
]


def saw_amplitudes(n_partials: int, slope: str = "saw") -> list:
    """slope=saw: pure 1/n. slope=brighter: 1/sqrt(n). slope=darker: 1/n^1.5."""
    if slope == "saw":
        return [1.0 / k for k in range(1, n_partials + 1)]
    if slope == "brighter":
        return [1.0 / (k ** 0.5) for k in range(1, n_partials + 1)]
    if slope == "darker":
        return [1.0 / (k ** 1.5) for k in range(1, n_partials + 1)]
    raise ValueError(slope)


def apply_bridge_hill_boost(amps: list, peak_partial: int, width: int,
                            strength: float) -> list:
    """Multiply partials around peak_partial by a small gaussian boost."""
    out = list(amps)
    for i in range(len(out)):
        d = i + 1 - peak_partial
        boost = 1.0 + strength * (2.71828 ** (-(d * d) / (2.0 * width * width)))
        out[i] = out[i] * boost
    return out


def make_patch(label: str, n_partials: int, slope: str,
               body: list, bridge_boost: float = 0.0,
               vibrato_depth: float = 0.0) -> dict:
    """Build a viola patch.

    bridge_boost: extra multiplier on partials around partial 10 (≈ 2.2kHz at
                  A220), simulating bridge-hill-driven harmonic emphasis even
                  before the Formant filter shapes things.
    vibrato_depth: optional, 0.0 = none, 0.005 = ±0.5% subtle, 0.01 = noticeable.
    """
    nodes = []

    nodes.append({"id": "ampEnv", "type": "Envelope",
                  "params": {"preset": "adsr", "attack": 0.12, "decay": 0.1,
                             "sustainLevel": 0.85, "release": 0.4}})

    # Body formants
    formant_refs = []
    for (fid, freq, gain, width) in body:
        nid = f"f_{fid}"
        nodes.append({"id": nid, "type": "Formant",
                      "params": {"frequency": freq, "gain": gain,
                                 "width": width, "power": 2.0}})
        formant_refs.append({"ref": nid})
    nodes.append({"id": "fspec", "type": "FormantSpectrum",
                  "params": {"formants": formant_refs}})

    # Source spectrum: 1/n bowed-string excitation
    amps = saw_amplitudes(n_partials, slope)
    if bridge_boost > 0:
        amps = apply_bridge_hill_boost(amps, peak_partial=10, width=3,
                                       strength=bridge_boost)
    mults = [float(k) for k in range(1, n_partials + 1)]

    nodes.append({"id": "fas_partials", "type": "ExplicitPartials",
                  "params": {
                      "mult1": mults, "mult2": mults,
                      "ampl1": amps,   "ampl2": amps,
                      # ampl arrays are authoritative; set rolloff=0 so the
                      # engine doesn't apply a second 1/n on top.
                      "rolloff1": 0.0, "rolloff2": 0.0,
                  }})

    # Optional subtle vibrato
    if vibrato_depth > 0:
        nodes.append({"id": "vib", "type": "Vibrato",
                      "params": {"speed": 5.0, "depth": vibrato_depth,
                                 "attack": 0.3, "threshold": 0.0,
                                 "speedVar": 0.0, "depthVar": 0.0}})
        freq_param = {"ref": "vib"}
    else:
        freq_param = FUNDAMENTAL

    nodes.append({"id": "fas", "type": "AdditiveSource",
                  "params": {
                      "seed": 42,
                      "frequency": freq_param,
                      "amplitude": {"ref": "ampEnv"},
                      "partials": {"ref": "fas_partials"},
                      "formant": {"ref": "fspec"},
                      "formantWeight": 1.0,
                  }})

    return {
        "sampleRate": 48000,
        "seconds": SECONDS,
        "graph": {"nodes": nodes, "output": "fas"},
        "instrument": {
            "paramMap": {"frequency": "fas.frequency" if vibrato_depth == 0
                         else "vib.var"},
            "polyphony": 1,
        },
        "score": [
            {"time": 0.0, "note": 57, "velocity": 0.8,
             "duration": SECONDS - 0.5},  # A3 = MIDI 57 = 220 Hz
        ],
    }


VARIANTS = [
    # (label, n_partials, slope,    body,         bridge_boost, vibrato)
    ("saw_16p_full",   16, "saw",      BODY_FULL,    0.0,  0.0),
    ("saw_24p_full",   24, "saw",      BODY_FULL,    0.0,  0.0),
    ("saw_32p_full",   32, "saw",      BODY_FULL,    0.0,  0.0),
    ("saw_24p_min",    24, "saw",      BODY_MINIMAL, 0.0,  0.0),
    ("saw_24p_bright", 24, "brighter", BODY_FULL,    0.0,  0.0),
    ("saw_24p_dark",   24, "darker",   BODY_FULL,    0.0,  0.0),
    ("saw_24p_brboost",24, "saw",      BODY_FULL,    0.6,  0.0),
    # vibrato variant dropped — Vibrato's param wiring needs different paramMap setup; revisit if interesting
]


def main():
    do_render = "--no-render" not in sys.argv

    PATCHES_DIR.mkdir(parents=True, exist_ok=True)
    out_render_dir = RENDERS_BASE / "viola_v2"
    out_render_dir.mkdir(parents=True, exist_ok=True)

    built = 0
    rendered = 0
    failures = []
    for (label, n_partials, slope, body, br_boost, vib) in VARIANTS:
        patch = make_patch(label, n_partials, slope, body, br_boost, vib)
        patch_path = PATCHES_DIR / f"viola_{label}.json"
        wav_path = out_render_dir / f"viola_{label}.wav"
        patch_copy_path = out_render_dir / f"viola_{label}.patch.json"

        with open(patch_path, "w") as f:
            json.dump(patch, f, indent=2)
        with open(patch_copy_path, "w") as f:
            json.dump(patch, f, indent=2)
        built += 1

        if do_render:
            res = subprocess.run(
                [str(CLI), str(patch_path), str(wav_path)],
                capture_output=True, text=True, cwd=str(REPO),
            )
            if res.returncode != 0 or not wav_path.exists():
                failures.append((label, res.returncode, res.stderr[-500:]))
            else:
                rendered += 1

    print(f"Built {built} patches.")
    print(f"Rendered {rendered} WAVs.")
    if failures:
        print("FAILURES:")
        for label, rc, err in failures:
            print(f"  {label}: rc={rc}\n    stderr: {err}")


if __name__ == "__main__":
    main()
