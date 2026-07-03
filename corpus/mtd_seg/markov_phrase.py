"""Combine 2 Markov figures into a phrase template + render to WAV. Phase 2 v1.

Samples 2 independent figures from the Phase-1 Markov model, randomly rolls a
combination (pattern x transform x placement), materializes the concrete figures,
emits an engine template JSON (verified motif/connector format), and renders to WAV
via mforce_cli --compose. No C++ changes; the chooser lives here.
"""
import argparse, json, pathlib, random, subprocess

REPO  = pathlib.Path(__file__).resolve().parent.parent.parent
CLI   = REPO / "build/tools/mforce_cli/Debug/mforce_cli.exe"
PATCH = REPO / "patches/Additive1.json"
MAJOR = [0, 2, 4, 5, 7, 9, 11]


# --------------------------------------------------------------------------- #
# Task 1: figure helpers
# --------------------------------------------------------------------------- #
def net_step(fig):
    return sum(u["step"] for u in fig["units"])


def invert(fig):
    return {"units": [{"duration": u["duration"], "step": -u["step"]}
                      for u in fig["units"]]}


def retrograde(fig):
    units = fig["units"]; n = len(units)
    durs = [u["duration"] for u in units]
    deg, c = [], 0
    for u in units:
        c += u["step"]; deg.append(c)            # cumulative degrees, deg[0]=0
    rdurs = durs[::-1]
    rdeg = deg[::-1]
    base = rdeg[0]
    rdeg = [d - base for d in rdeg]              # re-anchor so rdeg[0]=0
    rsteps = [0] + [rdeg[i] - rdeg[i-1] for i in range(1, n)]
    return {"units": [{"duration": rdurs[i], "step": rsteps[i]} for i in range(n)]}


# --------------------------------------------------------------------------- #
# Task 2: combination chooser
# --------------------------------------------------------------------------- #
PATTERNS = ["AB", "AAB", "AAAB", "ABAB", "AABB", "AAA'B", "ABA'B"]


def _tokens(pattern):
    # "AAA'B" -> ["A","A","P","B"]  (apostrophe turns the PRECEDING A into prime P)
    out = []
    for ch in pattern:
        if ch == "'":
            out[-1] = "P"
        else:
            out.append(ch)
    return out


def build_combination(figA, figB, pattern, transform, placement):
    refs = _tokens(pattern)
    motifs = {"A": figA, "B": figB}
    if "P" in refs:
        motifs["P"] = invert(figA) if transform == "invert" else retrograde(figA)
    connectors = [None]
    for i in range(1, len(refs)):
        prev = motifs[refs[i-1]]
        if refs[i] == refs[i-1] and refs[i] == "A":      # repeated-A seam: apply placement
            if placement == "same":
                connectors.append(-net_step(prev))
            elif placement == "sequence":
                connectors.append(-net_step(prev) + 2)
            else:                                        # climb
                connectors.append(0)
        else:
            connectors.append(0)                         # into a different motif: continue
    return motifs, refs, connectors


# --------------------------------------------------------------------------- #
# Task 3: template emitter
# --------------------------------------------------------------------------- #
def make_template(motifs, refs, connectors, *, key="C", scale="Major",
                  bpm=84.0, seed=1, start=("C", 4)):
    total = sum(u["duration"] for r in refs for u in motifs[r]["units"])
    figures = [{"source": "reference", "motifName": r} for r in refs]
    conns = [None if c is None else int(c) for c in connectors]
    return {
        "keyName": key, "scaleName": scale, "bpm": bpm, "masterSeed": seed,
        "motifs": [{"name": name, "figure": fig, "userProvided": True}
                   for name, fig in motifs.items()],
        "sections": [{"name": "Main", "beats": total}],
        "parts": [{
            "name": "melody", "role": "melody",
            "passages": {"Main": {
                "startingPitch": {"octave": start[1], "pitch": start[0]},
                "phrases": [{
                    "name": "P1",
                    "startingPitch": {"octave": start[1], "pitch": start[0]},
                    "figures": figures,
                    "connectors": conns,
                }],
            }},
        }],
    }


# --------------------------------------------------------------------------- #
# Task 4: render + pitch-verification harness
# --------------------------------------------------------------------------- #
def degree_to_semitone(d):
    octs, deg = divmod(d, 7)
    return 12 * octs + MAJOR[deg]


def predict_relative_semitones(motifs, refs, connectors):
    """Replay the engine's cursor math in scale-degrees; return semitones rel to first note."""
    degs, cursor = [], 0          # cursor = degree of previous note
    first = True
    for i, name in enumerate(refs):
        fig = motifs[name]
        lead = 0 if connectors[i] is None else connectors[i]
        anchor = 0 if first else cursor + lead   # first figure anchors at 0
        first = False
        d = 0
        for u in fig["units"]:
            d += u["step"]                       # degree within figure (unit0 step==0)
            degs.append(anchor + d)
        cursor = anchor + d                       # land on last note's degree
    semis = [degree_to_semitone(x) for x in degs]
    return [s - semis[0] for s in semis]


def render_template(template, out_prefix):
    tpath = REPO / (out_prefix + "_tmpl.json")    # the input template we author
    tpath.parent.mkdir(parents=True, exist_ok=True)
    tpath.write_text(json.dumps(template), encoding="utf-8")
    subprocess.run([str(CLI), "--compose", str(PATCH), str(REPO / out_prefix), "1",
                    "--template", str(tpath)], check=True, capture_output=True)
    ev = json.loads((REPO / (out_prefix + "_1.json")).read_text(encoding="utf-8"))
    return [int(e["data"]["noteNumber"]) for e in ev["parts"][0]["events"]]


# --------------------------------------------------------------------------- #
# Task 5: CLI driver
# --------------------------------------------------------------------------- #
def _sample_figure(model, rng, kmin=3, kmax=5):
    from markov_generate import gen_model
    k = rng.randint(kmin, kmax)
    step, pulse = gen_model(model, k, rng)
    return {"units": [{"duration": float(pulse[i]), "step": int(step[i])}
                      for i in range(len(step))]}


def main():
    from markov_model import MarkovModel
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=8)
    ap.add_argument("--seed", type=int, default=1234)
    ap.add_argument("--bpm", type=float, default=84.0)
    args = ap.parse_args()

    rng = random.Random(args.seed)
    model = MarkovModel.load()
    outdir = REPO / "renders/markov_phrases"
    outdir.mkdir(parents=True, exist_ok=True)

    for i in range(args.n):
        figA = _sample_figure(model, rng)
        figB = _sample_figure(model, rng)            # independent (contrast = TODO)
        pattern   = rng.choice(PATTERNS)
        transform = rng.choice(["invert", "retrograde"])
        placement = rng.choice(["same", "climb", "sequence"])
        motifs, refs, conns = build_combination(figA, figB, pattern, transform, placement)
        seed_i = args.seed * 1000 + i
        t = make_template(motifs, refs, conns, bpm=args.bpm, seed=seed_i)
        prefix = f"renders/markov_phrases/p{i:02d}_{pattern.replace(chr(39),'x')}"
        notes = render_template(t, prefix)
        print(f"p{i:02d}: {pattern:6} t={transform:9} place={placement:8} "
              f"{len(refs)} figs, {len(notes)} notes -> {prefix}_1.wav")


if __name__ == "__main__":
    main()
