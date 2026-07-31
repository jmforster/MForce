"""FM "modulate everything" matrix sweep (dsp BACKLOG item 7 stage 2, G3).

Matt's goal 3, verbatim: "Revisit FM (and its cousin PM) ... Try new things.
Modulate literally everything and see what happens!"

FMSource exposes six ValueSource params — frequency, amplitude, phase,
carrierRatio, modRatio, depth — and the engine lets ANY of them be driven by
any other source. Conventional FM modulates depth (an index envelope) and
leaves the ratios as constants; the ratios are exactly the parameters nobody
sweeps, because in a fixed-architecture FM synth they aren't reachable.

This generates a curated matrix over that unreached space:

  tier 1  one parameter modulated at a time, sub-audio and audio rate
  tier 2  several/all parameters modulated at once at incommensurate rates
  tier 3  rule-breakers (WORKFLOW: a few crazy ones per batch) — ultrasonic
          carriers, ratios crossing zero, irrational ratios, a modulator
          faster than its carrier, FM driving FM's own depth

Every patch shares one carrier pitch and one amplitude envelope so the batch
is comparable; only the modulation topology differs. Renders are ranked by
the novelty metric (research/novelty) and the survivors go to REVIEW — which
of these is MUSICAL is a taste question and is never decided here.

Writes patches/fm_matrix/. Render + rank via tools/_run_fm_matrix.py.
"""
import json
import os

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
OUT = os.path.join(REPO, "patches", "fm_matrix")
os.makedirs(OUT, exist_ok=True)

SR = 48000
SECONDS = 6
BASE_FREQ = 220.0


# --- node helpers -----------------------------------------------------------
def sine(nid, hz):
    return {"id": nid, "type": "SineSource",
            "params": {"frequency": hz, "amplitude": 1.0}}


def noise(nid, kind="RedNoiseSource", hz=8.0):
    """Red/wander noise as a smooth random modulator. `frequency` sets how
    fast it wanders for the noise types that take one."""
    return {"id": nid, "type": kind, "params": {"frequency": hz, "amplitude": 1.0}}


def rng(nid, lo, hi, var):
    """Map a [-1,1] wave source into [lo,hi] (normalized=false does the
    [-1,1]->[0,1] fold)."""
    return {"id": nid, "type": "RangeSource",
            "params": {"min": lo, "max": hi, "var": {"ref": var},
                       "normalized": False}}


def env(nid, start, end, etype="Expo", power=2.0):
    return {"id": nid, "type": "Envelope",
            "params": {"stages": [{"startVal": start, "endVal": end,
                                   "type": etype, "power": power,
                                   "percent": 0.0}]}}


def patch(nodes, fm_params, seconds=SECONDS, volume=0.7):
    fm = {"id": "fm", "type": "FMSource", "params": dict(fm_params)}
    fm["params"].setdefault("frequency", BASE_FREQ)
    fm["params"].setdefault("amplitude", {"ref": "ampEnv"})
    return {
        "sampleRate": SR, "seconds": seconds,
        "graph": {
            "nodes": [env("ampEnv", 1.0, 0.0, "Expo", 1.2)] + nodes + [
                fm,
                {"id": "ch1", "type": "SoundChannel",
                 "inputs": {"source": "fm"},
                 "params": {"volume": volume, "pan": 0.0}},
                {"id": "mix", "type": "StereoMixer",
                 "inputs": {"channels": ["ch1"]},
                 "params": {"gainL": 1.0, "gainR": 1.0}},
            ],
            "output": "mix",
        },
    }


# --- the matrix -------------------------------------------------------------
# (name, nodes, fm params). "*" prefix = deliberate rule-breaker.
CASES = []


def add(name, nodes, fmp, **kw):
    CASES.append((name, patch(nodes, fmp, **kw)))


# --- tier 1: one parameter at a time ---------------------------------------
# The reference point: textbook FM, constant ratios, index envelope.
add("t1_00_control_classic",
    [env("depthEnv", 8.0, 0.0, "Expo", 3.0)],
    {"carrierRatio": 1.0, "modRatio": 2.0, "depth": {"ref": "depthEnv"}})

# carrierRatio swept slowly: the carrier walks up the harmonic series while
# the modulator stays put — the sideband pattern re-centres continuously.
add("t1_01_cratio_lfo_slow",
    [sine("lfo", 0.4), rng("cr", 0.5, 6.0, "lfo"),
     env("depthEnv", 6.0, 2.0, "Expo", 2.0)],
    {"carrierRatio": {"ref": "cr"}, "modRatio": 1.0,
     "depth": {"ref": "depthEnv"}})

# modRatio swept slowly: sidebands breathe in and out of harmonicity.
add("t1_02_mratio_lfo_slow",
    [sine("lfo", 0.3), rng("mr", 0.5, 8.0, "lfo"),
     env("depthEnv", 6.0, 2.0, "Expo", 2.0)],
    {"carrierRatio": 1.0, "modRatio": {"ref": "mr"},
     "depth": {"ref": "depthEnv"}})

# modRatio at audio rate: the RATIO itself becomes an oscillator. There is no
# classic FM synth that can do this.
add("t1_03_mratio_audiorate",
    [sine("mod", 110.0), rng("mr", 1.0, 3.0, "mod"), env("depthEnv", 5.0, 3.0)],
    {"carrierRatio": 1.0, "modRatio": {"ref": "mr"},
     "depth": {"ref": "depthEnv"}})

# carrierRatio at audio rate.
add("t1_04_cratio_audiorate",
    [sine("mod", 73.0), rng("cr", 0.5, 4.0, "mod"), env("depthEnv", 4.0, 2.0)],
    {"carrierRatio": {"ref": "cr"}, "modRatio": 2.0,
     "depth": {"ref": "depthEnv"}})

# depth driven by a red-noise wander instead of an envelope: index flickers.
add("t1_05_depth_rednoise",
    [noise("rn", "RedNoiseSource", 12.0), rng("dp", 0.0, 14.0, "rn")],
    {"carrierRatio": 1.0, "modRatio": 1.41, "depth": {"ref": "dp"}})

# phase modulated at sub-audio: slow phase drift against the modulator.
add("t1_06_phase_lfo",
    [sine("lfo", 1.7), rng("ph", -1.0, 1.0, "lfo"), env("depthEnv", 6.0, 2.0)],
    {"carrierRatio": 1.0, "modRatio": 2.0, "phase": {"ref": "ph"},
     "depth": {"ref": "depthEnv"}})

# phase modulated at audio rate — this is PM stacked on top of FM (the
# "cousin" in Matt's goal), a second modulator the architecture never exposed.
add("t1_07_phase_audiorate_pm",
    [sine("pmod", 220.0), rng("ph", -0.5, 0.5, "pmod"),
     env("depthEnv", 4.0, 1.0)],
    {"carrierRatio": 1.0, "modRatio": 1.0, "phase": {"ref": "ph"},
     "depth": {"ref": "depthEnv"}})

# carrier frequency wandering on red noise: drifting, unstable pitch centre.
add("t1_08_freq_rednoise",
    [noise("rn", "RedNoiseSource", 3.0), rng("fq", 180.0, 260.0, "rn"),
     env("depthEnv", 6.0, 2.0)],
    {"frequency": {"ref": "fq"}, "carrierRatio": 1.0, "modRatio": 2.0,
     "depth": {"ref": "depthEnv"}})

# amplitude at audio rate = ring modulation layered over FM.
add("t1_09_amp_audiorate_ring",
    [sine("am", 137.0), rng("ag", 0.0, 1.0, "am"), env("depthEnv", 5.0, 2.0)],
    {"amplitude": {"ref": "ag"}, "carrierRatio": 1.0, "modRatio": 2.0,
     "depth": {"ref": "depthEnv"}})


# --- tier 2: modulate everything -------------------------------------------
# All six params on incommensurate sub-audio LFOs — nothing ever repeats.
add("t2_10_all_lfo_incommensurate",
    [sine("l1", 0.13), sine("l2", 0.29), sine("l3", 0.47),
     sine("l4", 0.71), sine("l5", 1.13),
     rng("fq", 200.0, 240.0, "l1"), rng("cr", 0.5, 3.0, "l2"),
     rng("mr", 0.5, 5.0, "l3"), rng("dp", 0.0, 10.0, "l4"),
     rng("ph", -1.0, 1.0, "l5")],
    {"frequency": {"ref": "fq"}, "carrierRatio": {"ref": "cr"},
     "modRatio": {"ref": "mr"}, "depth": {"ref": "dp"},
     "phase": {"ref": "ph"}})

# Same topology, noise instead of LFOs — every parameter wanders.
add("t2_11_all_noise_wander",
    [noise("n1", "RedNoiseSource", 2.0), noise("n2", "RedNoiseSource", 5.0),
     noise("n3", "WanderNoiseSource", 7.0), noise("n4", "RedNoiseSource", 11.0),
     rng("fq", 190.0, 250.0, "n1"), rng("cr", 0.5, 4.0, "n2"),
     rng("mr", 0.5, 6.0, "n3"), rng("dp", 0.0, 12.0, "n4")],
    {"frequency": {"ref": "fq"}, "carrierRatio": {"ref": "cr"},
     "modRatio": {"ref": "mr"}, "depth": {"ref": "dp"}})

# Both ratios at audio rate, depth on an envelope: a dense inharmonic cloud.
add("t2_12_both_ratios_audiorate",
    [sine("m1", 91.0), sine("m2", 157.0),
     rng("cr", 0.5, 3.0, "m1"), rng("mr", 0.5, 5.0, "m2"),
     env("depthEnv", 9.0, 1.0, "Expo", 2.5)],
    {"carrierRatio": {"ref": "cr"}, "modRatio": {"ref": "mr"},
     "depth": {"ref": "depthEnv"}})

# Ratios locked in opposition: as the carrier ratio rises the mod ratio falls,
# so the modulator/carrier interval sweeps through unison from both sides.
add("t2_13_ratio_counter_sweep",
    [sine("lfo", 0.25), rng("cr", 0.5, 5.0, "lfo"), rng("mr", 5.0, 0.5, "lfo"),
     env("depthEnv", 7.0, 3.0)],
    {"carrierRatio": {"ref": "cr"}, "modRatio": {"ref": "mr"},
     "depth": {"ref": "depthEnv"}})

# Oversampled version of the densest case — uses the run-8 alias suppression
# so the high-index content is real content, not fold-over.
add("t2_14_dense_oversampled8",
    [sine("m1", 91.0), sine("m2", 157.0),
     rng("cr", 0.5, 3.0, "m1"), rng("mr", 0.5, 5.0, "m2"),
     env("depthEnv", 20.0, 2.0, "Expo", 2.5)],
    {"carrierRatio": {"ref": "cr"}, "modRatio": {"ref": "mr"},
     "depth": {"ref": "depthEnv"}, "oversample": 8})


# --- tier 3: rule-breakers --------------------------------------------------
# Ultrasonic carrier (well above Nyquist) with a swept mod ratio — the spacy
# family's lineage, where the audible result is entirely fold-down.
add("*t3_15_ultrasonic_carrier",
    [sine("lfo", 0.2), rng("mr", 0.8, 2.4, "lfo"),
     env("depthEnv", 12.0, 0.0, "Expo", 3.0)],
    {"frequency": 150000.0, "carrierRatio": 1.0, "modRatio": {"ref": "mr"},
     "depth": {"ref": "depthEnv"}, "unbounded_pos": True}, seconds=10)

# modRatio swept THROUGH zero and negative: the modulator stops, reverses, and
# runs backwards. Undefined territory for a ratio parameter.
add("*t3_16_mratio_through_zero",
    [sine("lfo", 0.35), rng("mr", -3.0, 3.0, "lfo"), env("depthEnv", 8.0, 4.0)],
    {"carrierRatio": 1.0, "modRatio": {"ref": "mr"},
     "depth": {"ref": "depthEnv"}})

# Irrational ratios swept against each other — no partial ever lands on a
# harmonic, and the beating never closes.
add("*t3_17_irrational_pair",
    [sine("l1", 0.19), sine("l2", 0.31),
     rng("cr", 1.0, 3.14159, "l1"), rng("mr", 1.618, 2.71828, "l2"),
     env("depthEnv", 9.0, 3.0)],
    {"carrierRatio": {"ref": "cr"}, "modRatio": {"ref": "mr"},
     "depth": {"ref": "depthEnv"}})

# Modulator of the ratio running FASTER than the carrier itself (1.7 kHz
# modulating a 220 Hz carrier's ratio).
add("*t3_18_modulator_above_carrier",
    [sine("fast", 1700.0), rng("mr", 0.5, 4.0, "fast"),
     env("depthEnv", 5.0, 2.0)],
    {"carrierRatio": 1.0, "modRatio": {"ref": "mr"},
     "depth": {"ref": "depthEnv"}})

# FM driving FM: an entire FMSource is the depth modulator. Recursive
# modulation is the FM analogue of Matt's recursive-partial-expansion hint.
add("*t3_19_fm_drives_fm_depth",
    [{"id": "fmMod", "type": "FMSource",
      "params": {"frequency": 3.0, "amplitude": 1.0, "carrierRatio": 1.0,
                 "modRatio": 2.5, "depth": 4.0}},
     rng("dp", 0.0, 16.0, "fmMod")],
    {"carrierRatio": 1.0, "modRatio": 1.5, "depth": {"ref": "dp"}})

# Extreme index (0..60) with no oversampling: maximum aliasing, kept as the
# deliberate A-side against t2_14's oversampled version.
add("*t3_20_index60_aliased",
    [env("depthEnv", 60.0, 0.0, "Expo", 2.0)],
    {"carrierRatio": 1.0, "modRatio": 2.41, "depth": {"ref": "depthEnv"}})

# Same, oversampled 16x — the B-side.
add("*t3_21_index60_oversampled16",
    [env("depthEnv", 60.0, 0.0, "Expo", 2.0)],
    {"carrierRatio": 1.0, "modRatio": 2.41, "depth": {"ref": "depthEnv"},
     "oversample": 16})

# Carrier frequency itself modulated at audio rate by a source at an
# unrelated frequency, on top of the FM — three nested pitch modulations.
add("*t3_22_triple_nested_pitch",
    [sine("v1", 6.0), rng("v2r", 40.0, 900.0, "v1"),
     {"id": "v2", "type": "SineSource",
      "params": {"frequency": {"ref": "v2r"}, "amplitude": 1.0}},
     rng("fq", 120.0, 400.0, "v2"),
     env("depthEnv", 7.0, 2.0)],
    {"frequency": {"ref": "fq"}, "carrierRatio": 1.0, "modRatio": 2.0,
     "depth": {"ref": "depthEnv"}})

# Phase stepped by velvet noise: sparse impulsive phase jumps in an otherwise
# steady FM tone — clicks that are part of the oscillator, not layered on.
add("*t3_23_phase_velvet_jumps",
    [noise("vn", "VelvetNoiseSource", 20.0), rng("ph", -1.0, 1.0, "vn"),
     env("depthEnv", 5.0, 3.0)],
    {"carrierRatio": 1.0, "modRatio": 2.0, "phase": {"ref": "ph"},
     "depth": {"ref": "depthEnv"}})


def main():
    for name, p in CASES:
        fn = os.path.join(OUT, name.lstrip("*") + ".json")
        json.dump(p, open(fn, "w"), indent=1)
    breakers = sum(1 for n, _ in CASES if n.startswith("*"))
    print(f"wrote {len(CASES)} patches to {OUT} ({breakers} rule-breakers)")


if __name__ == "__main__":
    main()
