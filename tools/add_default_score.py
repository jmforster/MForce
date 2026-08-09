#!/usr/bin/env python3
"""Add a single-note default score to patches that have an `instrument`
block but no `score`. Without a score, the CLI's instrument-mode render
fires no notes and produces silent WAVs.

The note duration matches the patch's `seconds` field (or 3.0 default).
Pitch defaults to MIDI 60 (middle C) — the actual rendered pitch comes
through paramMap, so this is just "play one note for the full duration."

Idempotent.
"""
import json
import sys
from pathlib import Path


def process(path):
    with open(path) as f:
        doc = json.load(f)
    if "instrument" not in doc:
        return False
    if "score" in doc:
        return False
    seconds = float(doc.get("seconds", 3.0))
    # Leave a small tail for envelope release.
    note_duration = max(0.5, seconds - 0.5)
    doc["score"] = [
        {
            "time": 0.0,
            "note": 60,        # middle C; the paramMap controls real pitch
            "velocity": 0.8,
            "duration": note_duration,
        }
    ]
    with open(path, "w") as f:
        json.dump(doc, f, indent=2)
    return True


def main():
    if len(sys.argv) < 2:
        print("Usage: add_default_score.py <dir-or-file> [...]")
        sys.exit(1)
    added = 0
    seen = 0
    for t in sys.argv[1:]:
        p = Path(t)
        paths = [p] if p.is_file() else list(p.rglob("*.json"))
        for path in paths:
            seen += 1
            try:
                if process(path):
                    added += 1
            except Exception as e:
                print(f"  [error] {path}: {e}", file=sys.stderr)
    print(f"Seen: {seen}  Added score: {added}")


if __name__ == "__main__":
    main()
