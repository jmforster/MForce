# ref_match — YouTube reference-match pipeline (salvaged 2026-09-08)

The working scripts from the 09-07 clarinet/bassoon sessions, copied
verbatim from the session scratchpad before it evaporated. They extend
oboe_ref_compare.py (one dir up) into a repeatable workflow:

1. `yt-dlp -x --audio-format wav -o "<scratch>\NAME.%(ext)s" <url>`
2. Recon: pitch-track the ref (drop fmin to ~70 for low instruments),
   read off key / register / tempo / player intonation.
3. `*_match.py` — builds the OTJ phrase score (transposable via `base`
   MIDI note and `eighth` seconds), renders the patch, runs the full
   comparison (LTAS peaks, matched-note harmonic envelope, noise gap,
   vibrato, attack).
4. `*_sweep4.py` — directed-cell pattern: mutator functions + per-cell
   metrics. bassoon_sweep4 has the octave-band scoring (`bands()`), the
   better raucousness lens; clar_sweep4 has `fast_articulation`
   (seconds-mode envelope surgery) and the threshold bisection.
5. `bassoon_attrib.py` — the mute-one-leg attribution pattern for
   "where does X reach the output".

PATH WARNING: these hardcode a session scratchpad for the downloaded
reference WAVs — re-download the ref and fix `SCRATCH`/`REF` before
reuse. Module-level code runs on import in some (the trap is documented
in BASSOON_REF_ANALYSIS lessons); prefer copying functions.

Findings these produced: OBOE_REF_ANALYSIS, CLARINET_REF_ANALYSIS,
BASSOON_REF_ANALYSIS (all in docs/research/feedback_sweeps/), each
ending in plain-language lessons.
