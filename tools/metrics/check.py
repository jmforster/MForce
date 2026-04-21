#!/usr/bin/env python3
"""Pre-listen metrics — render health + timbral fingerprint.

Usage:
    python tools/metrics/check.py <wav-path>

Emits <wav-basename>.features.json next to the input wav.
Exit codes: 0=OK, 1=WARN, 2=FAIL, 64=tool error.
"""

import sys

def main():
    if len(sys.argv) != 2:
        print("Usage: check.py <wav-path>", file=sys.stderr)
        return 64
    wav_path = sys.argv[1]
    # Real implementation lands in Task 2.
    print(f"check.py: not implemented yet for {wav_path}")
    return 64

if __name__ == "__main__":
    sys.exit(main())
