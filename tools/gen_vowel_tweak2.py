"""Pass-2 compare ladder for the vowel entries (dsp run 24).

Built on the run-23 winners (Matt's verdict 2026-08-06):
  Alto_A_v3_narrow ("very close"), Alto_E_v1_open, Alto_I_v2_f2up,
  Alto_U_v1_speechUW ("very close"), Bass_U_v3_midF2, Tenor_U_v3_midF2,
  Soprano_A_v1_wide, Soprano_I_v1_highF2, Soprano_U_v2_h2mid.
  Soprano_E: no winner ("needs another approach").
  Remaining complaints: U's read OO not UW; sopranos have partial "ring";
  Alto E/I "hear partials a bit".

Measured grounding for the U problem (scratch measure_u.py, 2026-08-06):
  Speech_M_UW (the UW Matt likes) = F1 h3 392 at 0 dB, F2 h8 1047 at -11,
  PLUS a strong F3 peak h18 2355 at -14.7 and a broad F4/F5 shelf holding
  -16..-23 dB all the way to 4.2 kHz (3.5-4.5k band energy -15.7 dB rel).
  The sung-U winners have F1/F2 placement right but their tops are dead:
  Bass 3.5-4.5k band -83.8 dB, Tenor -44.4, Alto -38.3, Soprano -43.1.
  Speech_M_OO's top is actually BRIGHTER than UW's, so within the speech pair
  OO-vs-UW is an F1/F2-frequency contrast — but the sung U's already sit at
  low F1/F2. Hypothesis: the missing F3 peak + upper shelf is what makes the
  sung U's read lax/dull ("OO"); ladder THAT (u* variants).

Ring mitigation (soprano r* variants): the additive engine's bandwidth
(per-partial AM noise, Loris-style) and motion (per-partial cents walks)
layers were never used by the static grid. Escalation if none works:
vibrato via the pitch-mod layer (out of scope this run).

Each variant = one hypothesis. Winner (or grid, for Soprano_E) copied as the
A/B anchor per family. Writes patches/vowel_tweak2/, renders
renders/vowel_tweak2/, verifies predictions (peak positions/levels; line
widths for ring variants). Usage: python tools/gen_vowel_tweak2.py [--no-render]
"""
import json
import math
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
GRID = os.path.join(REPO, "patches", "vowel_grid")
TW1 = os.path.join(REPO, "patches", "vowel_tweak")
TW1_R = os.path.join(REPO, "renders", "vowel_tweak")
OUT = os.path.join(REPO, "patches", "vowel_tweak2")
OUT_R = os.path.join(REPO, "renders", "vowel_tweak2")
CLI = os.path.join(REPO, "build", "tools", "mforce_cli", "Release",
                   "mforce_cli.exe")

F0 = {"Sing_Bass": 110.0, "Sing_Tenor": 164.81, "Sing_Alto": 220.0,
      "Sing_Soprano": 440.0}

# Speech_M_UW's upper structure (patch values) — the measured missing piece.
UW_F3 = (2355.0, 0.1614, 880.0)
UW_F4 = (3378.0, 0.1125, 2000.0)
UW_F5 = (3750.0, 0.1013, 1600.0)

# entry -> (base_patch_file, winner_wav_or_None, [(variant, formant_overrides,
#            partials_cfg, hypothesis, listen_for)])
# formant_overrides: {formant_id: (freq, gain, width)}; partials_cfg goes into
# the "fp" node (bandwidth/motion configs). Widths are ENGINE widths.
FAMILIES = {
    # ---- winner refinements (slight steps) --------------------------------
    "Sing_Alto_A": ("vowel_tweak/Sing_Alto_A_v3_narrow.json",
                    "Sing_Alto_A_v3_narrow.wav", [
        ("w1_f2soft", {"f2": (1150.0, 0.5, 440.0)}, {},
         "winner minus 2 dB on the F2 side of the plateau (gain .631->.50) -> "
         "h5/h6 drop ~2 dB, h4 stays the peak",
         "same 'ah', slightly darker upper mid; pick if winner is a touch honky"),
        ("w2_f2hot", {"f2": (1150.0, 0.79, 440.0)}, {},
         "winner plus 2 dB on F2 (gain .631->.79) -> h5 pulls within ~2 dB of "
         "h4; brighter open 'ah'",
         "same 'ah', slightly brighter; pick if winner is a touch covered"),
        ("w3_tight", {"f1": (800.0, 1.0, 400.0), "f2": (1150.0, 0.631, 400.0)},
         {},
         "winner widths 440->400 -> plateau edges (h3/h6) drop ~2-3 dB, "
         "peaks more resolved",
         "slightly clearer two-peak shape; risk: thinner"),
    ]),
    "Sing_Alto_E": ("vowel_tweak/Sing_Alto_E_v1_open.json",
                    "Sing_Alto_E_v1_open.wav", [
        ("w1_bwlight", {}, {"bandwidth1": 0.06, "bandwidth2": 0.06,
                            "bandwidthHz": 45.0},
         "winner + 6% per-partial AM noise (45 Hz band) -> each harmonic line "
         "widens from ~2 Hz to tens of Hz; vowel pattern unchanged",
         "same 'eh', lines slightly breathy/fused — does the partial "
         "audibility drop?"),
        ("w2_fill", {"f1": (600.0, 1.0, 560.0), "f2": (1540.0, 0.25, 680.0)},
         {},
         "winner widths up (480->560, 560->680) -> neighbors h2 440 and h6 "
         "1320 come up ~3-5 dB, isolated peaks get company",
         "same 'eh', denser; fewer standout single partials"),
        ("w3_bw_fill", {"f1": (600.0, 1.0, 560.0), "f2": (1540.0, 0.25, 680.0)},
         {"bandwidth1": 0.06, "bandwidth2": 0.06, "bandwidthHz": 45.0},
         "fill + blur combined",
         "densest of the three; pick if w1/w2 each help but not enough"),
    ]),
    "Sing_Alto_I": ("vowel_tweak/Sing_Alto_I_v2_f2up.json",
                    "Sing_Alto_I_v2_f2up.wav", [
        ("w1_bwlight", {}, {"bandwidth1": 0.06, "bandwidth2": 0.06,
                            "bandwidthHz": 45.0},
         "winner + 6% AM-noise blur, formants untouched",
         "same 'ee', lines fused; does the partial audibility drop?"),
        ("w2_fill", {"f2": (2100.0, 0.25, 880.0)}, {},
         "F2 width 720->880 -> h8 1760 and h11 2420 come up, the bright "
         "cluster becomes 3-4 harmonics instead of 2",
         "same 'ee', fuller top; fewer standout single partials"),
        ("w3_bw_fill", {"f2": (2100.0, 0.25, 880.0)},
         {"bandwidth1": 0.06, "bandwidth2": 0.06, "bandwidthHz": 45.0},
         "fill + blur combined",
         "densest 'ee' of the three"),
    ]),
    "Sing_Alto_U": ("vowel_tweak/Sing_Alto_U_v1_speechUW.json",
                    "Sing_Alto_U_v1_speechUW.wav", [
        ("w1_f2exact", {"f2": (1100.0, 0.4, 560.0)}, {},
         "F2 1151->1100, dead-on h5 -> peak gains ~1 dB, skirt on h6 drops; "
         "removes the slight mistuning",
         "same u, marginally cleaner second peak"),
        ("w2_f2soft", {"f2": (1151.0, 0.32, 560.0)}, {},
         "F2 gain .40->.32 -> h5 down ~2 dB, darker u",
         "slightly darker u; pick if winner leans o-ward/w-ward"),
        ("w3_f3touch", {"f3": (2355.0, 0.08, 880.0)}, {},
         "half-strength Speech_M_UW F3 (grid f3 was 2500/.02) -> h11 2420 "
         "rises to ~ -23 dB; the tense-u top, gently",
         "same u with a hint of brightness at 2.4k — more 'boot', less 'book'?"),
    ]),
    # ---- the U problem: give the sung U's the UW top ----------------------
    "Sing_Bass_U": ("vowel_tweak/Sing_Bass_U_v3_midF2.json",
                    "Sing_Bass_U_v3_midF2.wav", [
        ("u1_f3peak", {"f3": UW_F3}, {},
         "adopt Speech_M_UW's F3 (2355/.16/880) verbatim; winner's best top "
         "harmonic was -27 -> new resolved peak h21/h22 ~2355 at ~ -16",
         "does the 2.4k peak alone flip OO->UW?"),
        ("u2_uwtop", {"f3": UW_F3, "f4": UW_F4, "f5": UW_F5}, {},
         "full Speech_M_UW upper structure (F3 peak + F4/F5 shelf to 4.2k); "
         "winner's 3.5-4.5k band was -84 dB, UW's is -16 -> shelf at -17..-23",
         "closest spectral match to the reference UW; A/B against "
         "_REF_Speech_M_UW"),
        ("u3_top_half", {"f3": (2355.0, 0.08, 880.0),
                         "f4": (3378.0, 0.056, 2000.0),
                         "f5": (3750.0, 0.05, 1600.0)}, {},
         "same shape at half gain (-6 dB) -> peak ~ -22, shelf ~ -23..-29; "
         "how much top is enough?",
         "between winner and u2; pick if u2 reads speechy/buzzy"),
    ]),
    "Sing_Tenor_U": ("vowel_tweak/Sing_Tenor_U_v3_midF2.json",
                     "Sing_Tenor_U_v3_midF2.wav", [
        ("u1_f3peak", {"f3": UW_F3, "f4": (2900.0, 0.0501, 960.0),
                       "f5": (3300.0, 0.0251, 960.0)}, {},
         "UW F3 2355 + tenor grid F4/F5 cut to move the top's center of "
         "gravity down (winner's top peaked at 2967, UW peaks at 2355)",
         "does re-centering the existing top at 2.4k flip OO->UW?"),
        ("u2_uwtop", {"f3": UW_F3, "f4": UW_F4, "f5": UW_F5}, {},
         "full Speech_M_UW upper structure; extends the shelf past the "
         "winner's 3.4k stop to 4.2k",
         "closest spectral match to the reference UW"),
        ("u3_top_half", {"f3": (2355.0, 0.08, 880.0),
                         "f4": (3378.0, 0.056, 2000.0),
                         "f5": (3750.0, 0.05, 1600.0)}, {},
         "UW top at half gain",
         "between winner and u2"),
    ]),
    "Sing_Soprano_U": ("vowel_tweak/Sing_Soprano_U_v2_h2mid.json",
                       "Sing_Soprano_U_v2_h2mid.wav", [
        ("u1_f3peak", {"f3": (2200.0, 0.16, 1200.0)}, {},
         "UW-strength F3 centered on h5 2200 (nearest harmonic to UW's 2355; "
         "width 1200 lets h6 2640 catch the skirt) -> h5 ~ -16, h6 ~ -23",
         "does the 2.2k peak flip OO->UW at soprano f0?"),
        ("u2_uwtop", {"f3": (2200.0, 0.16, 1200.0), "f4": UW_F4, "f5": UW_F5},
         {},
         "F3 peak + UW F4/F5 shelf -> h7 3080 / h8 3520 / h9 3960 lit at "
         "~ -19..-21 (winner had nothing above -36)",
         "closest match to the reference UW; more partials = more ring risk — "
         "that trade is the point of this rung"),
    ]),
    # ---- Soprano_E: three NEW approaches (all prior variants failed) ------
    "Sing_Soprano_E": ("grid:Sing_Soprano_E", None, [
        ("e1_twopeak", {"f1": (440.0, 1.0, 480.0), "f2": (1760.0, 0.3, 640.0),
                        "f3": (2200.0, 0.15, 640.0)}, {},
         "two-peak E: h4 1760 AND h5 2200 both lit with a 6 dB down-tilt "
         "(h4 -10.5, h5 -16.5); prior attempts lit only one mid harmonic. "
         "E's 1.8-2.2k region shaped as a tilted pair, vs I = single high peak",
         "listen for 'eh' vs Soprano_I_v1_highF2: E = two mids tilted down, "
         "I = one peak at 2640"),
        ("e2_h1dom", {"f1": (440.0, 1.0, 480.0), "f2": (1760.0, 0.12, 640.0),
                      "f3": (2800.0, 0.02, 960.0)}, {},
         "F1-side identity: E differentiated from I by the h1:mid ratio "
         "instead of mid placement — mid at h4 -18 (I winner's mid is -10.5), "
         "F3 sheen killed; h1 dominance reads as the more open vowel",
         "darker, rounder 'eh'; if this reads E, identity at 440 lives in "
         "h1:mid balance, not peak position"),
        ("e3_altoXpose", {"f1": (1200.0, 1.0, 960.0),
                          "f2": (3080.0, 0.25, 1120.0),
                          "f3": (2800.0, 0.02, 960.0)}, {},
         "the working Alto_E_v1_open recipe transposed x2 with intervals "
         "preserved (F1 600->1200 = 2.7x f0, F2 1540->3080 = 7x f0, widths "
         "x2). h1 440 sits below F1's skirt and goes SILENT — exactly like "
         "the alto winner's h1 220 does; peak h3 1320, second peak h7 3080",
         "most radical: no fundamental, peak at h3 — does the alto E's "
         "interval structure carry the vowel up an octave?"),
    ]),
    # ---- soprano ring mitigation: blur ladders on the winners -------------
    "Sing_Soprano_A": ("vowel_tweak/Sing_Soprano_A_v1_wide.json",
                       "Sing_Soprano_A_v1_wide.wav", [
        ("r1_bwlight", {}, {"bandwidth1": 0.06, "bandwidth2": 0.06,
                            "bandwidthHz": 45.0},
         "6% per-partial AM noise, 45 Hz band -> each line widens from ~2 Hz "
         "(window-limited) to ~tens of Hz at -12 dB; formant pattern intact",
         "same 'ah'; do the 440-spaced partials fuse instead of ringing?"),
        ("r2_bwmotion", {}, {"bandwidth1": 0.06, "bandwidth2": 0.06,
                             "bandwidthHz": 45.0, "motionDepth1": 7.0,
                             "motionDepth2": 7.0, "motionHz": 1.2,
                             "motionCoherence": 0.85, "motionEvolve": 0.2},
         "blur + slow mostly-coherent pitch wander (~6 cents common drift at "
         "1.2 Hz, ~1 cent independent) -> partials move together like a "
         "voice, small extra line width",
         "'ah' with life; the coherent drift should read vocal, not chorus"),
        ("r3_bwstrong", {}, {"bandwidth1": 0.25, "bandwidth2": 0.25,
                             "bandwidthHz": 150.0},
         "25% AM noise, 150 Hz band -> sidebands at -4.8 dB rel each line, "
         "spread +-150 Hz: the noise skirts actually reach toward the 440 Hz "
         "gaps; risk: breathy/noisy",
         "max blur short of vibrato; pick if r1 is too subtle"),
    ]),
    "Sing_Soprano_I": ("vowel_tweak/Sing_Soprano_I_v1_highF2.json",
                       "Sing_Soprano_I_v1_highF2.wav", [
        ("r1_bwlight", {}, {"bandwidth1": 0.06, "bandwidth2": 0.06,
                            "bandwidthHz": 45.0},
         "6% AM-noise blur on the winner", "same 'ee', fused lines"),
        ("r2_bwmotion", {}, {"bandwidth1": 0.06, "bandwidth2": 0.06,
                             "bandwidthHz": 45.0, "motionDepth1": 7.0,
                             "motionDepth2": 7.0, "motionHz": 1.2,
                             "motionCoherence": 0.85, "motionEvolve": 0.2},
         "blur + slow coherent wander", "'ee' with life"),
        ("r3_bwstrong", {}, {"bandwidth1": 0.25, "bandwidth2": 0.25,
                             "bandwidthHz": 150.0},
         "25% AM noise, 150 Hz band -> wide noise skirts",
         "max blur short of vibrato"),
    ]),
}
# Soprano_U ring rungs ride on the same winner as its u* rungs; add them here
# so the family has both ladders.
FAMILIES["Sing_Soprano_U"][2].extend([
    ("r1_bwlight", {}, {"bandwidth1": 0.06, "bandwidth2": 0.06,
                        "bandwidthHz": 45.0},
     "6% AM-noise blur on the winner", "same u, fused lines"),
    ("r2_bwmotion", {}, {"bandwidth1": 0.06, "bandwidth2": 0.06,
                         "bandwidthHz": 45.0, "motionDepth1": 7.0,
                         "motionDepth2": 7.0, "motionHz": 1.2,
                         "motionCoherence": 0.85, "motionEvolve": 0.2},
     "blur + slow coherent wander", "u with life"),
    ("r3_bwstrong", {}, {"bandwidth1": 0.25, "bandwidth2": 0.25,
                         "bandwidthHz": 150.0},
     "25% AM noise, 150 Hz band -> wide noise skirts",
     "max blur short of vibrato"),
])

REFS = ["Speech_M_UW"]


def load_base(spec):
    if spec.startswith("grid:"):
        name = spec.split(":", 1)[1]
        with open(os.path.join(GRID, name + ".json")) as f:
            patch = json.load(f)
        # Same normalization pass 1 applied to retune-flagged sopranos:
        # bake effective F1 (= f0 440) and drop the retune curve.
        node(patch, "f1")["params"]["frequency"] = 440.0
        patch["instrument"]["paramMap"]["frequency"] = "src.frequency"
        return patch
    with open(os.path.join(REPO, "patches", spec)) as f:
        return json.load(f)


def node(patch, nid):
    for n in patch["graph"]["nodes"]:
        if n["id"] == nid:
            return n
    raise KeyError(nid)


def apply_variant(patch, fmt_ov, cfg_ov):
    for fid, (fr, g, w) in fmt_ov.items():
        p = node(patch, fid)["params"]
        p["frequency"], p["gain"], p["width"] = fr, round(g, 4), w
    if cfg_ov:
        node(patch, "fp")["params"].update(cfg_ov)
    return patch


def taper(f, F, gain, w):
    lo, hi = F - w / 2.0, F + w / 2.0
    if f <= lo or f >= hi:
        return 0.0
    s = (f - lo) / (F - lo) if f < F else (hi - f) / (hi - F)
    return s * s * gain


def predicted(patch, f0, fmax=5000.0):
    fmts = [node(patch, f"f{i}")["params"] for i in range(1, 6)]
    ks = range(1, int(fmax / f0) + 1)
    return [max(taper(k * f0, fm["frequency"], fm["gain"], fm["width"])
                for fm in fmts) for k in ks]


def render(ppath, wpath):
    err = ""
    for attempt in range(6):
        try:
            r = subprocess.run([CLI, ppath, wpath], capture_output=True,
                               text=True)
            err = r.stderr.strip()[:200]
        except OSError as e:  # exe locked mid-relink by another agent
            err = str(e)
        if os.path.exists(wpath):
            return True
        time.sleep(3.0)
    print("RENDER FAILED:", ppath, err)
    return False


def load_wav(wpath):
    import numpy as np
    import wave
    with wave.open(wpath, "rb") as w:
        sr, n, ch, sw = (w.getframerate(), w.getnframes(), w.getnchannels(),
                         w.getsampwidth())
        raw = w.readframes(n)
    dt = {2: np.int16, 4: np.int32}[sw]
    x = np.frombuffer(raw, dtype=dt).astype(np.float64)
    if ch > 1:
        x = x.reshape(-1, ch).mean(axis=1)
    return x / np.iinfo(dt).max, sr


def measured(wpath, f0, fmax=5000.0):
    import numpy as np
    x, sr = load_wav(wpath)
    a, b = int(0.5 * sr), int(min(2.5 * sr, len(x)))
    seg = x[a:b] * np.hanning(b - a)
    t = np.arange(b - a) / sr
    ks = range(1, int(fmax / f0) + 1)
    # Band energy (peak psd bin +- 0.45*f0), not sine projection: bandwidth /
    # motion variants spread line energy into sidebands the projection misses.
    n = b - a
    spec = np.abs(np.fft.rfft(seg)) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    out = []
    for k in ks:
        m = (freqs > k * f0 - 0.45 * f0) & (freqs < k * f0 + 0.45 * f0)
        out.append(float(np.sqrt(spec[m].sum())))
    return out, float(np.sqrt(np.mean(x ** 2)))


def blur_metrics(wpath, f0, k, drop_db=25.0):
    """(width_hz, gap_db) for harmonic k.

    width_hz: spectral width at drop_db below the line's peak — first run
    showed the AM-noise pedestal sits ~25-35 dB under the peak bin, so a
    -12 dB width never moves; -25 dB catches the pedestal shoulder.
    gap_db: energy in the inter-harmonic band (k+0.3..k+0.7)*f0 relative to
    the harmonic's own band — the direct "is the 440 Hz gap filling in"
    number for ring mitigation.
    """
    import numpy as np
    x, sr = load_wav(wpath)
    a, b = int(0.5 * sr), int(min(2.5 * sr, len(x)))
    seg = x[a:b] * np.hanning(b - a)
    n = b - a
    psd = np.abs(np.fft.rfft(seg)) ** 2
    freqs = np.fft.rfftfreq(n, 1.0 / sr)
    m = np.where((freqs > k * f0 - 0.45 * f0) & (freqs < k * f0 + 0.45 * f0))[0]
    if not len(m):
        return 0.0, 0.0
    pk = m[np.argmax(psd[m])]
    thr = psd[pk] / (10 ** (drop_db / 10.0))
    lo = pk
    while lo > m[0] and psd[lo - 1] >= thr:
        lo -= 1
    hi = pk
    while hi < m[-1] and psd[hi + 1] >= thr:
        hi += 1
    width = float(freqs[hi] - freqs[lo])
    gm = (freqs > (k + 0.3) * f0) & (freqs < (k + 0.7) * f0)
    hm = (freqs > k * f0 - 0.45 * f0) & (freqs < k * f0 + 0.45 * f0)
    gap = 10 * math.log10(psd[gm].sum() / (psd[hm].sum() + 1e-30) + 1e-12)
    return width, float(gap)


def peaks_db(vals, f0, floor_db=-45.0):
    import numpy as np
    v = np.array(vals)
    db = 20 * np.log10(v / (v.max() + 1e-30) + 1e-12)
    out = []
    for i in range(len(v)):
        if db[i] < floor_db:
            continue
        if (i == 0 or v[i] >= v[i - 1]) and (i == len(v) - 1 or v[i] > v[i + 1]):
            out.append((i + 1, round((i + 1) * f0), round(float(db[i]), 1)))
    return out


def main():
    do_render = "--no-render" not in sys.argv[1:]
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(OUT_R, exist_ok=True)
    manifest, results, ring_rows = [], [], []

    for name, (base_spec, winner_wav, variants) in FAMILIES.items():
        f0 = F0[name.rsplit("_", 1)[0]]
        # anchor: winner wav (or grid render for Soprano_E)
        if winner_wav:
            src = os.path.join(TW1_R, winner_wav)
            anchor = os.path.join(OUT_R, name + "_v0_winner.wav")
        else:
            src = os.path.join(REPO, "renders", "vowel_grid", name + ".wav")
            anchor = os.path.join(OUT_R, name + "_v0_grid.wav")
        if os.path.exists(src):
            shutil.copyfile(src, anchor)
        if name == "Sing_Soprano_E":  # closest prior attempt as 2nd anchor
            p1 = os.path.join(TW1_R, "Sing_Soprano_E_v1_lowF2.wav")
            if os.path.exists(p1):
                shutil.copyfile(p1, os.path.join(
                    OUT_R, "Sing_Soprano_E_v0_prior_lowF2.wav"))

        for vname, fmt_ov, cfg_ov, hyp, listen in variants:
            patch = apply_variant(load_base(base_spec), fmt_ov, cfg_ov)
            full = f"{name}_{vname}"
            ppath = os.path.join(OUT, full + ".json")
            with open(ppath, "w") as f:
                json.dump(patch, f, indent=1)
            pred = predicted(patch, f0)
            manifest.append({
                "file": full + ".json", "entry": name, "variant": vname,
                "f0": f0, "base": base_spec, "hypothesis": hyp,
                "listen_for": listen,
                "formant_overrides": {k: {"freq": v[0], "gain": v[1],
                                          "width": v[2]}
                                      for k, v in fmt_ov.items()},
                "partials_cfg": cfg_ov,
                "predicted_peaks": peaks_db(pred, f0)})
            if do_render:
                wpath = os.path.join(OUT_R, full + ".wav")
                if render(ppath, wpath):
                    meas, rms = measured(wpath, f0)
                    results.append((full, f0, rms, pred, meas))
                    if vname.startswith("r") and os.path.exists(anchor):
                        # blur check on the anchor's two strongest harmonics:
                        # lines must widen / gaps must fill, pattern must hold.
                        am, _ = measured(anchor, f0)
                        import numpy as np
                        top2 = list(np.argsort(am)[-2:][::-1] + 1)
                        for k in top2:
                            w0, g0 = blur_metrics(anchor, f0, int(k))
                            w1, g1 = blur_metrics(wpath, f0, int(k))
                            ring_rows.append((full, int(k), w0, w1, g0, g1))

    for r in REFS:
        src = os.path.join(REPO, "renders", "vowel_grid", r + ".wav")
        if os.path.exists(src):
            shutil.copyfile(src, os.path.join(OUT_R, "_REF_" + r + ".wav"))

    with open(os.path.join(OUT, "_manifest.json"), "w") as f:
        json.dump(manifest, f, indent=1)

    if do_render:
        import numpy as np
        print(f"{'variant':<34}{'rms':>7}{'corr':>7}  peaks: predicted | measured")
        for full, f0, rms, pred, meas in results:
            p, m = np.array(pred), np.array(meas)
            corr = float(np.corrcoef(p / (p.max() + 1e-30),
                                     m / (m.max() + 1e-30))[0, 1])
            pp = ", ".join(f"h{k}({hz}){db:+.0f}" for k, hz, db in
                           peaks_db(pred, f0, -30))
            mm = ", ".join(f"h{k}({hz}){db:+.0f}" for k, hz, db in
                           peaks_db(meas, f0, -30))
            print(f"{full:<34}{rms:>7.4f}{corr:>7.3f}  {pp}  |  {mm}")
        if ring_rows:
            print("\nblur check (anchor -> variant): width@-25dB Hz | "
                  "gap energy dB rel harmonic")
            for full, k, w0, w1, g0, g1 in ring_rows:
                print(f"  {full:<34} h{k:<3} {w0:6.1f} -> {w1:6.1f} | "
                      f"{g0:+6.1f} -> {g1:+6.1f}")
    print(f"\n{len(manifest)} variants, {len(FAMILIES)} families")


if __name__ == "__main__":
    main()
