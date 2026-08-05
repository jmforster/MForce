"""Generate the SlewLimiterSource click ladder (backlog 3g / REVIEW 14).

Takes the run-20 click PoC (velvet impulses -> FM phase) and inserts a
SlewLimiterSource in Peak mode between the phase RangeSource and FMSource,
at a fallRate ladder.

The physics of the dial: the impulse jumps phase by 0.5 cycles and then
glides linearly back to rest over 0.5/fallRate seconds. A linear phase glide
IS a constant frequency offset, so during the glide the carrier is detuned by
fallRate/2 Hz. Bigger fallRate therefore means a SHORTER but more
FREQUENCY-DEVIANT chirp; smaller means a longer, gentler slide. That trade is
the point — it is not a plain brightness knob.

Usage: python tools/gen_slew_clicks.py
"""
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "patches" / "fm_clicks" / "poc1_attack_amp400.json"
OUTDIR = ROOT / "patches" / "slew_clicks"

# fallRate (units/s) -> glide length for a 0.5-cycle jump, and the implied
# frequency offset during the glide.
LADDER = [50.0, 200.0, 1000.0, 5000.0, 20000.0]


def build(fall_rate):
    patch = json.loads(SRC.read_text())
    nodes = patch["graph"]["nodes"]

    # Insert the limiter downstream of the phase RangeSource "ph".
    limiter = {
        "id": "slew",
        "type": "SlewLimiterSource",
        "params": {
            "source": {"ref": "ph"},
            "mode": 2,              # Peak — impulses, not steps
            "rate": 1000000.0,      # effectively instant attack
            "fallRate": fall_rate,
        },
    }
    idx = next(i for i, n in enumerate(nodes) if n["id"] == "ph")
    nodes.insert(idx + 1, limiter)

    # Re-point FMSource.phase at the limiter.
    fm = next(n for n in nodes if n["id"] == "fm")
    assert fm["params"]["phase"] == {"ref": "ph"}, fm["params"]["phase"]
    fm["params"]["phase"] = {"ref": "slew"}
    return patch


def main():
    OUTDIR.mkdir(parents=True, exist_ok=True)
    written = []
    for fr in LADDER:
        name = f"slewclick_f{int(fr):05d}.json"
        (OUTDIR / name).write_text(json.dumps(build(fr), indent=1) + "\n")
        glide_ms = 0.5 / fr * 1000.0
        written.append((name, fr, glide_ms, fr / 2.0))

    # Control: the unmodified PoC, copied so the A/B pair sits in one folder.
    ctrl = json.loads(SRC.read_text())
    (OUTDIR / "slewclick_ctrl_none.json").write_text(json.dumps(ctrl, indent=1) + "\n")

    print(f"{'patch':32} {'fallRate':>9} {'glide ms':>9} {'df Hz':>8}")
    print(f"{'slewclick_ctrl_none.json':32} {'-':>9} {'0 (step)':>9} {'-':>8}")
    for name, fr, ms, df in written:
        print(f"{name:32} {fr:9.0f} {ms:9.3f} {df:8.0f}")


if __name__ == "__main__":
    main()
