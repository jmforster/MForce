# patches/library/voice — locked sung-vowel patches

Curated keepers. Everything here has passed Matt's ears and is not to be
edited in place — iterate in `patches/sweep/` or `patches/pending/` and
promote a new file when a verdict says so.

Locked by `tools/lock_vowel_winners.py` (verbatim copy + a `_provenance`
block; the block is inert to the loader).

## Provenance — Matt's verdict, dsp/REVIEW.md item 23, 2026-08-09

> Diminishing returns. Only 1 improved on the previous "winners", and that
> very marginally. Lock new Tenor_U_u1_f3peak.

| Locked patch | Source | f0 | Note |
|---|---|---|---|
| `sing_alto_a.json` | vowel_tweak/Sing_Alto_A_v3_narrow | 220 | pass-2 w1/w2/w3 all sounded identical to it |
| `sing_alto_e.json` | vowel_tweak/Sing_Alto_E_v1_open | 220 | pass-2 blur variants added wobble |
| `sing_alto_i.json` | vowel_tweak/Sing_Alto_I_v2_f2up | 220 | pass-2 no improvement |
| `sing_alto_u.json` | vowel_tweak/Sing_Alto_U_v1_speechUW | 220 | pass-2 no improvement |
| `sing_bass_u.json` | vowel_tweak/Sing_Bass_U_v3_midF2 | 110 | pass-2 UW-top grafts did not beat it |
| `sing_soprano_a.json` | vowel_tweak/Sing_Soprano_A_v1_wide | 440 | pass-2 ring ladder no improvement |
| `sing_tenor_u.json` | **vowel_tweak2/Sing_Tenor_U_u1_f3peak** | 146.8 | **the one pass-2 win** — UW's F3 peak (2355 Hz, -14.7 dB) grafted onto the pass-1 winner |

Verified: all 7 render byte-identical (sha256) to the exact WAVs Matt
auditioned, so the locked file is the approved sound and not a
re-derivation. Renders land in `renders/library/voice/` (gitignored).

## NOT locked — still unsatisfactory (dsp BACKLOG 16)

Soprano E, Soprano I, Soprano U. Matt's words:

- **Soprano E** — "all sound like I except _altoXpose which sounds like a
  pennywhistle"
- **Soprano I** — "all new attempts inferior to _winner but _winner too
  partially/organy"
- **Soprano U** — "all new attempts inferior to _winner but _winner lacks
  vowel character"

See `docs/autonomy/dsp/reports/2026-08-10-dipsy-run25.md` for the measured
diagnosis of why all three fail at f0 = 440.
