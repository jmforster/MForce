"""FM matrix follow-up batch: bracket + effect-attack variants of the 11
patches Matt marked Y in the fm_matrix audition.

Spec (Matt): "make 2 folders, one for modulated and one for ramped. In
modulated, bracket 3 versions of each (lo/med/hi). In ramped, ramp the
effect from 1 to 0 for 0.1, 0.2, and 0.5 second attacks."

Interpretation:
  modulated/  per patch, the PRIMARY effect parameter (the modulation range
              that defines the topology) at three intensities: med = the
              original value, lo ~ 0.4x, hi ~ 2.5x (clamped per param so
              ratios stay >= 0.1, phase stays within +-1 cycle, AM depth
              maxes out at full bipolar ring).
  ramped/     the effect intensity ramps 1 -> 0 over 0.1 / 0.2 / 0.5 s at
              note start, then stays off. Implemented by wiring the driver
              oscillator/noise AMPLITUDE through an explicit-stage Envelope
              (1 -> 0 over T, then hold 0); with amplitude 0 the RangeSource
              collapses to its range center, i.e. a static FM tone = effect
              off. For the two envelope-driven patches (t1_00 control,
              t3_20 index60) the index envelope itself is compressed to T.

Mechanism notes:
  - RangeSource (normalized=false): out = min + (v+1)/2*(max-min), so a
    driver with amplitude w gives out = center + w*s*halfwidth. w=0 -> the
    exact range center. That is what makes the amplitude-envelope ramp a
    clean "effect off" switch.
  - Shared rampEnv across several drivers is safe: the patch loader wraps
    2nd+ consumers in RefSource (read-only, no double-advance).
  - Envelope stage duration pinned with minSec == maxSec == T.

Writes patches/fm_matrix2/{modulated,ramped}/. Render + verify via
tools/_run_fm_matrix2.py.
"""
import copy
import json
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
SRC = os.path.join(REPO, "patches", "fm_matrix")
DST_MOD = os.path.join(REPO, "patches", "fm_matrix2", "modulated")
DST_RAMP = os.path.join(REPO, "patches", "fm_matrix2", "ramped")
os.makedirs(DST_MOD, exist_ok=True)
os.makedirs(DST_RAMP, exist_ok=True)

RAMPS = [(0.1, "ramp01"), (0.2, "ramp02"), (0.5, "ramp05")]


def load(name):
    with open(os.path.join(SRC, name + ".json")) as f:
        return json.load(f)


def node(p, nid):
    for n in p["graph"]["nodes"]:
        if n["id"] == nid:
            return n
    raise KeyError(nid)


# ---------------------------------------------------------------------------
# Per-patch bracket spec.
#   ranges: {node_id: {"lo": (min,max), "hi": (min,max)}}  (RangeSource edits;
#           med = original range, untouched)
#   env_peak: (env_node_id, lo_start, hi_start)  (index-envelope startVal)
#   drivers: node ids whose amplitude gets the ramp envelope in ramped/
#   env_ramp: (env_node_id,) -> compress that envelope's stage to T seconds
#
# Bracket policy: med = original. lo scales the modulation half-width by 0.4
# about the range center; hi by 2.5, clamped so ratio params keep min >= 0.1
# (symmetric clamp — half-width shrinks to keep the center). Ranges anchored
# at 0 (depth, unipolar amp) scale the max instead. Phase clamped to a full
# cycle. Ring-mod amp hi goes bipolar [-1,1] (true ring, carrier suppressed).
# ---------------------------------------------------------------------------
SPEC = {
    "t1_00_control_classic": {
        # No exotic effect — bracket/ramp the classic FM index envelope
        # (depth 8 -> 0 Expo) as the "effect", per instruction.
        "env_peak": ("depthEnv", 3.2, 20.0),
        "env_ramp": "depthEnv",
    },
    "t1_03_mratio_audiorate": {
        # modRatio driven by 110 Hz sine over [1,3] (center 2, hw 1).
        # hi hw 2.5 clamped to 1.9 so min stays 0.1.
        # ANNOTATION: lo < med orders on measurement (sidebin count 70 -> 135,
        # entropy 2.80 -> 3.54) but hi does NOT — tried [0.1,3.9] and
        # [0.5,5.5], both non-monotonic in gross spectral metrics. Physical,
        # not a wiring bug: when mr sweeps near zero/unison the sidebands
        # momentarily collapse, so a wider swing is a qualitatively different
        # effect, not uniformly "more". Kept with that caveat.
        "ranges": {"mr": {"lo": (1.6, 2.4), "hi": (0.1, 3.9)}},
        "drivers": ["mod"],
    },
    "t1_04_cratio_audiorate": {
        # carrierRatio driven by 73 Hz sine over [0.5,4] (center 2.25,
        # hw 1.75). hi hw 4.375 clamped to 2.15.
        "ranges": {"cr": {"lo": (1.55, 2.95), "hi": (0.1, 4.4)}},
        "drivers": ["mod"],
    },
    # t1_07_phase_audiorate_pm: SKIPPED. FMSource::compute_wave_value uses its
    # own carrierPhase_/modPhase_ accumulators and never reads the WaveSource
    # phase_ param — the wired "ph" modulation is dead, all variants render
    # byte-identical (verified by md5). The sound Matt marked Y is plain 1:1
    # FM with the depth envelope; there is no phase effect to bracket or ramp
    # without engine changes (out of scope for this batch).
    "t1_09_amp_audiorate_ring": {
        # AM depth: amplitude driven by 137 Hz sine over [0,1] (100% AM).
        # hi = bipolar [-1,1]: true 4-quadrant ring mod, carrier suppressed
        # (2x depth is the physical max here).
        "ranges": {"ag": {"lo": (0.3, 0.7), "hi": (-1.0, 1.0)}},
        "drivers": ["am"],
    },
    "t2_11_all_noise_wander": {
        # Wander depth on all four noise-driven params scales together.
        "ranges": {
            "fq": {"lo": (208.0, 232.0), "hi": (145.0, 295.0)},
            "cr": {"lo": (1.55, 2.95),   "hi": (0.1, 4.4)},
            "mr": {"lo": (2.15, 4.35),   "hi": (0.1, 6.4)},
            "dp": {"lo": (0.0, 4.8),     "hi": (0.0, 30.0)},
        },
        "drivers": ["n1", "n2", "n3", "n4"],
    },
    "t2_12_both_ratios_audiorate": {
        # Both ratio mod widths scale together (91 / 157 Hz sines).
        "ranges": {
            "cr": {"lo": (1.25, 2.25), "hi": (0.1, 3.4)},
            "mr": {"lo": (1.85, 3.65), "hi": (0.1, 5.4)},
        },
        "drivers": ["m1", "m2"],
    },
    "t2_14_dense_oversampled8": {
        # Same topology as t2_12 (depth env 20->2, 8x oversample kept as-is);
        # same primary param: both ratio mod widths.
        "ranges": {
            "cr": {"lo": (1.25, 2.25), "hi": (0.1, 3.4)},
            "mr": {"lo": (1.85, 3.65), "hi": (0.1, 5.4)},
        },
        "drivers": ["m1", "m2"],
    },
    "t3_18_modulator_above_carrier": {
        # modRatio driven by 1.7 kHz sine over [0.5,4].
        "ranges": {"mr": {"lo": (1.55, 2.95), "hi": (0.1, 4.4)}},
        "drivers": ["fast"],
    },
    "t3_19_fm_drives_fm_depth": {
        # Recursive depth-mod range [0,16] anchored at 0: scale the max.
        "ranges": {"dp": {"lo": (0.0, 6.4), "hi": (0.0, 40.0)}},
        "drivers": ["fmMod"],
    },
    "t3_20_index60_aliased": {
        # Extreme-index envelope (60 -> 0 Expo, deliberately aliased).
        "env_peak": ("depthEnv", 24.0, 150.0),
        "env_ramp": "depthEnv",
    },
}


def make_modulated(name, spec):
    for level in ("lo", "med", "hi"):
        p = load(name)
        if level != "med":
            for nid, lohi in spec.get("ranges", {}).items():
                mn, mx = lohi[level]
                node(p, nid)["params"]["min"] = mn
                node(p, nid)["params"]["max"] = mx
            if "env_peak" in spec:
                nid, lo, hi = spec["env_peak"]
                stg = node(p, nid)["params"]["stages"][0]
                stg["startVal"] = lo if level == "lo" else hi
        fn = os.path.join(DST_MOD, f"{name}_{level}.json")
        json.dump(p, open(fn, "w"), indent=1)


def ramp_env_node(T, seconds):
    """Explicit-stage envelope: 1 -> 0 over exactly T seconds, then hold 0."""
    return {"id": "rampEnv", "type": "Envelope", "params": {"stages": [
        {"startVal": 1.0, "endVal": 0.0, "type": "Linear", "power": 0.0,
         "percent": T / seconds, "minSec": T, "maxSec": T},
        {"startVal": 0.0, "endVal": 0.0, "type": "Linear", "power": 0.0,
         "percent": 0.0},
    ]}}


def make_ramped(name, spec):
    for T, tag in RAMPS:
        p = load(name)
        seconds = float(p["seconds"])
        if "env_ramp" in spec:
            # Envelope-driven effect: compress the index envelope to T
            # (same start value and curve), then hold 0.
            env = node(p, spec["env_ramp"])
            stg = copy.deepcopy(env["params"]["stages"][0])
            stg["percent"] = T / seconds
            stg["minSec"] = T
            stg["maxSec"] = T
            env["params"]["stages"] = [
                stg,
                {"startVal": 0.0, "endVal": 0.0, "type": "Linear",
                 "power": 0.0, "percent": 0.0},
            ]
        else:
            p["graph"]["nodes"].insert(0, ramp_env_node(T, seconds))
            for nid in spec["drivers"]:
                node(p, nid)["params"]["amplitude"] = {"ref": "rampEnv"}
        fn = os.path.join(DST_RAMP, f"{name}_{tag}.json")
        json.dump(p, open(fn, "w"), indent=1)


def main():
    for name, spec in SPEC.items():
        make_modulated(name, spec)
        make_ramped(name, spec)
    n = len(SPEC)
    print(f"wrote {n * 3} modulated -> {DST_MOD}")
    print(f"wrote {n * 3} ramped   -> {DST_RAMP}")


if __name__ == "__main__":
    main()
