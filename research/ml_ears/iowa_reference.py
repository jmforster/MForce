"""Precompute an instrument reference target for the CMA-ES scorer (stage a).

Driven by an instrument config (configs/<name>.json, default viola): writes
<reference_path> holding, for the open-string scoring notes, the per-note
harmonic envelope, broadband ratios, and attack stats; plus motion medians
pooled over the config's wider motion_samples set.

Run once per instrument (samples don't change); score_candidate.py loads the
JSON so no eval re-analyses the .aif files. Regenerate if the metric
extractors change.

Usage: python iowa_reference.py [--config viola]
"""
import json
import os
import sys

import numpy as np
import soundfile as sf


def rm_rise90(atk, sr):
    """Milliseconds from onset to 90% of the attack window's peak |x|."""
    a = np.abs(atk)
    if a.size == 0 or a.max() <= 0:
        return 0.0
    return float(np.argmax(a >= 0.9 * a.max())) / sr * 1000.0

import derive_motion as dm
import refmetrics as rm
import score_candidate as sc

HERE = os.path.dirname(os.path.abspath(__file__))


def find_file(sample_dir, string, note, ext=".aif"):
    for f in os.listdir(sample_dir):
        if f".{string}." in f and f".{note}." in f and f.endswith(ext):
            return os.path.join(sample_dir, f)
    raise FileNotFoundError(f"{string} {note} in {sample_dir}")


def load_mono(path):
    x, sr = sf.read(path)
    if x.ndim > 1:
        x = x.mean(axis=1)
    return x.astype(np.float64), sr


def onset_index(x, sr, frac=0.02, win_s=0.005):
    """Sample index of note onset: first crossing of `frac` of the peak of a
    `win_s` RMS envelope. Piano sample lead-in varies 0.04-0.54 s, so configs
    with reference_build.onset_relative=true get their analysis windows sliced
    relative to this instead of the file start."""
    win = max(1, int(win_s * sr))
    env = np.sqrt(np.convolve(x * x, np.ones(win) / win, "same"))
    return int(np.argmax(env >= frac * env.max()))


def main():
    name = (sys.argv[sys.argv.index("--config") + 1]
            if "--config" in sys.argv else "viola")
    cfg = sc.load_config(name)
    sample_dir = cfg["sample_dir"]
    ext = cfg.get("sample_glob", "*.aif").lstrip("*")
    rb = cfg["reference_build"]
    sus_start, sus_len = rb["sus_start"], rb["sus_len"]
    attack_len = rb["attack_len"]
    onset_rel = bool(rb.get("onset_relative", False))
    # reference_build.inharmonic (opt-in, piano only): measure each note's
    # stiff-string B and analyse at the STRETCHED partial positions
    # n*f0*sqrt(1+B n^2) instead of at k*f0. Absent -> B is never measured and
    # never stored, so every other instrument regenerates byte-identically
    # (partial_freq at B=0 multiplies by exactly 1.0).
    inharmonic = bool(rb.get("inharmonic", False))
    out = cfg["reference_path"]

    notes = {}
    for sn in cfg["score_notes"]:
        note, string = sn["note"], sn["string"]
        x, sr = load_mono(find_file(sample_dir, string, note, ext))
        if onset_rel:
            n_on = onset_index(x, sr)
            print(f"{note:3s} onset at {n_on / sr:.3f}s", end="  ")
            x = x[n_on:]
        midi = sn.get("midi", rm.note_to_midi(note))
        # exact equal-tempered f0 from midi (config f0 is documentation /
        # fallback only — the rounded value must not shift analysis bins)
        f0 = rm.midi_to_freq(midi) if midi is not None else sn["f0"]
        n0 = int(sus_start * sr)
        sus = x[n0: n0 + int(sus_len * sr)]
        atk = x[: int(attack_len * sr)]
        B, lines = 0.0, None
        if inharmonic:
            # f0 stays the equal-tempered value: the tracker's refined f0 is a
            # by-product of the fit and letting it move would shift every
            # analysis bin for a reason unrelated to stretch.
            lines, B, n_tracked = rm.measured_lines(sus, sr, f0)
            if not lines:
                lines = None
        notes[note] = {
            "midi": midi,
            "f0": round(f0, 3),
            "harm_env_db": [round(v, 3)
                            for v in rm.harmonic_env(sus, sr, f0, B=B)],
            "broadband": [round(v, 6)
                          for v in rm.broadband_ratios(sus, sr, f0, B=B,
                                                       lines_hz=lines)],
            "attack": [[round(a, 5), round(b, 5)]
                       for a, b in rm.attack_stats(atk, sr)],
            # 2026-08-12 scorer v2 (Matt's v6i audit): rise-to-90%-of-peak in
            # ms (the old band-lag features scored 0.08 while the render was
            # 7x slower than Iowa), and early RMS for register level balance
            # (Iowa mf bass is ~2.5x louder than treble; the flat target was
            # backwards).
            "rise90_ms": round(float(rm_rise90(atk, sr)), 2),
            "early_rms": round(float(np.sqrt(np.mean(
                x[:int(0.5 * sr)] ** 2) + 1e-20)), 8),
        }
        if inharmonic:
            notes[note]["B"] = float(f"{B:.6g}")
            # Stored so the candidate is masked on the SAME line positions the
            # reference was measured on, rather than on a model refitted per
            # candidate.
            notes[note]["lines_hz"] = [round(v, 2) for v in (lines or [])]
        m = rm.motion_stats(sus, sr, f0, B=B)
        bstr = (f" B={B:.3e} lines={len(lines or [])}({n_tracked} tracked)"
                if inharmonic else "")
        print(f"{note:3s} f0={f0:6.1f}{bstr}  "
              f"broadband={notes[note]['broadband']}  "
              f"resid={m['resid_cents_rms']:.2f}c amp={100*m['amp_frac_rms']:.0f}%")

    # Motion medians pooled over the config's broader sample set (more robust
    # than the score notes alone; motion is roughly note-invariant).
    # motion_window (optional, reference_build): [start, len] seconds replacing
    # derive_motion's defaults — decaying instruments need an earlier/shorter
    # window than a bowed sustain.
    mw = rb.get("motion_window")
    rows = []
    for tag in cfg.get("motion_samples", []):
        string, note = tag.split(".")
        f0_nom = dm.note_to_freq(note)
        try:
            path = find_file(sample_dir, string, note, ext)
            off = 0.0
            if onset_rel:
                xm, srm = load_mono(path)
                off = onset_index(xm, srm) / srm
            rows.append(dm.analyze(path, f0_nom, offset=off,
                                   start=mw[0] if mw else None,
                                   length=mw[1] if mw else None))
        except Exception as e:  # noqa: BLE001
            print(f"  motion {tag}: FAILED ({e})")
    motion_med = {k: float(np.median([r[k] for r in rows])) for k in rm.MOTION_KEYS}

    ref = {"notes": notes, "motion_medians": motion_med,
           "source": cfg.get("source", cfg["name"]), "sus_start": sus_start,
           "sus_len": sus_len}
    json.dump(ref, open(out, "w"), indent=2)
    print("\nmotion medians:", {k: round(v, 3) for k, v in motion_med.items()})
    print("wrote", out)


if __name__ == "__main__":
    main()
