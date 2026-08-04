"""Build a scorer baseline (corpus_stats_<name>.json) from ANY of the three
ingested corpora — comp backlog #4 remainder, the run-10 caveat.

Why this exists: score_generated's "ears" were anchored on MTD (classical
B&M themes) only, so scoring a folk generation against them measured
"how classical is this folk tune", not "how plausible is this folk tune".
Nottingham and Essen were ingested as (scale-step, pulse) TOKENS, which
can't feed the semitone-domain features — so this script re-parses each
corpus from source through its own parser and computes the identical
feature set:

  mtd         corpus/mtd_full/midi/*_score.mid       (score_generated.mtd_melodies)
  nottingham  corpus/nottingham-dataset/MIDI/*.mid   (melody track picked per tune)
  essen       corpus/essen-folksong-collection/**/*.krn

Melodies are (onset_beats, midi_pitch, dur_beats) triples, exactly what
score_generated.features() expects.

Usage:
  python corpus_baseline.py [mtd|nottingham|essen|all] [--limit N] [--compare]

--compare prints the three baselines side by side (no rebuild if cached).
"""
import json
import random
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import score_generated as sg                                    # noqa: E402
from essen_tokenize import KERN_ROOT, DIV, parse_kern           # noqa: E402
from nottingham_tokenize import (MIDI_DIR as NOTT_DIR,          # noqa: E402
                                 parse_midi_tracks, pick_melody)

CORPORA = ("mtd", "nottingham", "essen", "essen_europa", "essen_asia")

# File order matters when a segment limit truncates the feed. Essen's tree is
# africa/ america/ asia/ europa/, so alphabetical order fed 2,246 ASIAN
# (largely Chinese, pentatonic) tunes before a single European one — the first
# build anchored "folk" on Chinese folksong and its interval JSD vs MTD (0.128)
# was measuring that, not "folk vs classical". Every feed is now shuffled with
# a fixed seed so a truncated sample is representative and reproducible.
FILE_SEED = 20260731


def shuffled(files):
    files = sorted(files)
    random.Random(FILE_SEED).shuffle(files)
    return files


# ------------------------------------------------------------- melody feeds
def nottingham_melodies():
    for f in shuffled(NOTT_DIR.glob("*.mid")):
        try:
            div, _keysig, tracks = parse_midi_tracks(f)
            mel = pick_melody(tracks)
        except Exception:  # noqa: BLE001 — malformed midi, skip (same as ingest)
            continue
        if not mel:
            continue
        yield [(o / div, p, d / div) for o, p, d in mel]


def essen_melodies(subset=None):
    root = KERN_ROOT / subset if subset else KERN_ROOT
    for f in shuffled(root.rglob("*.krn")):
        try:
            notes, _keysig = parse_kern(f)
        except Exception:  # noqa: BLE001 — kern parse errors, skip (same as ingest)
            continue
        if not notes:
            continue
        yield [(o / DIV, p, d / DIV) for o, p, d in notes]


FEEDS = {
    "mtd": sg.mtd_melodies,
    "nottingham": nottingham_melodies,
    "essen": essen_melodies,
    # Essen's two big regional blocks, separately anchorable: europa = 6,213
    # tunes, asia = 2,246. Kept as first-class corpora because they are NOT
    # one tradition and averaging them makes a blurrier ruler than either.
    "essen_europa": lambda: essen_melodies("europa"),
    "essen_asia": lambda: essen_melodies("asia"),
}

# Length matters. MTD is a THEME corpus (median 18 notes); Nottingham and
# Essen are WHOLE-TUNE corpora (median 162 / 62). Half the baseline stats are
# length-sensitive — rep_LxCount counts repeated pulse patterns and so grows
# with length (whole-tune Nottingham rep_p10 = 50 vs MTD 6), and max_run_frac
# / selfsim shrink with length. Anchoring a 20-note generated phrase on
# whole-tune stats would fail the repetition screen by construction. So long
# corpus melodies are cut into theme-length windows before aggregation.
WINDOW = 18          # MTD median note count
WINDOW_SLACK = 1.5   # melodies under WINDOW*slack pass through whole
MIN_WINDOW = 8       # same floor stats_from_melodies uses

# MTD is already theme-length, and its baseline is the ruler every number
# reported since run 2 was measured against — leave it unwindowed so those
# stay comparable. Segment limits: 1200 themes for MTD (unchanged), 4000
# segments for the windowed corpora so segment count doesn't collapse tune
# coverage (~440 Nottingham tunes / ~1150 Essen tunes).
PER_CORPUS = {
    "mtd":          {"window": 0,      "limit": sg.CORPUS_LIMIT},
    "nottingham":   {"window": WINDOW, "limit": 4000},
    "essen":        {"window": WINDOW, "limit": 4000},
    "essen_europa": {"window": WINDOW, "limit": 4000},
    "essen_asia":   {"window": WINDOW, "limit": 4000},
}


def windowed(melodies, w=WINDOW):
    if not w:
        yield from melodies
        return
    for mel in melodies:
        if len(mel) <= w * WINDOW_SLACK:
            yield mel
            continue
        for i in range(0, len(mel), w):
            chunk = mel[i:i + w]
            if len(chunk) >= MIN_WINDOW:
                yield chunk


# ------------------------------------------------------------------- build
def build(corpus, limit=None, window=None):
    cfg = PER_CORPUS[corpus]
    limit = cfg["limit"] if limit is None else limit
    window = cfg["window"] if window is None else window
    stats = sg.stats_from_melodies(windowed(FEEDS[corpus](), window),
                                   limit=limit)
    stats["window"] = window
    # Closure anchors (#12) must come from WHOLE tunes: a window's last note
    # is a chopping artifact, not a phrase ending. So recompute those keys
    # from the unwindowed feed and overwrite the segment-derived ones.
    # (Measured gap recorded below after the first build.)
    if window:
        whole = sg.stats_from_melodies(FEEDS[corpus](), limit=limit)
        for k in sg.CLOSURE_KEYS:
            stats[k] = whole[k]
        stats["closure_n"] = whole["n_themes"]
    out = sg.cache_path(corpus)
    out.write_text(json.dumps(stats))
    print(f"[{corpus}] {stats['n_themes']} segments (window={window}) "
          f"-> {out.name}")
    return stats


ROWS = [
    ("n_themes", "{}"), ("window", "{}"), ("rep_median", "{}"), ("rep_p10", "{}"),
    ("big_leap_p90", "{:.3f}"), ("range_p10", "{}"), ("range_p90", "{}"),
    ("zero_rate_p95", "{:.3f}"), ("max_run_frac_p95", "{:.3f}"),
    ("selfsim_p95", "{:.3f}"),
    ("final_ratio_p25", "{:.2f}"), ("final_ratio_p50", "{:.2f}"),
    ("final_ratio_p95", "{:.2f}"), ("p_final_longest", "{:.3f}"),
    ("p_final_onset_int", "{:.3f}"),
]


def compare():
    loaded = {}
    for c in CORPORA:
        p = sg.cache_path(c)
        if p.exists():
            loaded[c] = json.loads(p.read_text())
    if not loaded:
        print("no baselines built yet")
        return
    names = [c for c in CORPORA if c in loaded]
    print(f"{'stat':>18s}" + "".join(f"{c:>13s}" for c in names))
    for key, fmt in ROWS:
        print(f"{key:>18s}"
              + "".join(f"{fmt.format(loaded[c].get(key, 0)):>13s}"
                        for c in names))
    # Interval-distribution distance between the anchors themselves: how much
    # the choice of anchor moves the int_jsd ruler.
    print("\ninter-anchor int_jsd (how different the rulers are):")
    for i, a in enumerate(names):
        for b in names[i + 1:]:
            ha = {int(k): v for k, v in loaded[a]["int_hist"].items()}
            hb = {int(k): v for k, v in loaded[b]["int_hist"].items()}
            print(f"  {a:>11s} vs {b:<11s} {sg.jsd(ha, hb):.3f}")


def main():
    args = list(sys.argv[1:])
    limit = window = None
    for flag in ("--limit", "--window"):
        if flag in args:
            i = args.index(flag)
            val = int(args[i + 1])
            if flag == "--limit":
                limit = val
            else:
                window = val
            del args[i:i + 2]
    do_compare = "--compare" in args
    args = [a for a in args if not a.startswith("--")]
    targets = CORPORA if (not args or args[0] == "all") else args
    if not do_compare or args:
        for c in targets:
            if c not in FEEDS:
                raise SystemExit(f"unknown corpus '{c}' (have {CORPORA})")
            build(c, limit, window)
    compare()


if __name__ == "__main__":
    main()
