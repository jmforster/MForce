# Dipsy — 2026-08-21 (MISC.md, Opus 4.8 interactive-autonomous)

Three items off `docs/autonomy/dsp/MISC.md` — the trivialities Matt queued for
this session. All three landed; both engine changes are byte-identical at their
defaults (proven, not asserted), and the new behaviour is verified to engage.

Session start: on Opus 4.8 (confirmed powered-by). `grep MATT` over REVIEW.md +
GOALS.md clean of actionable entries. Branch main, tree clean of engine files.
No concurrent lane in engine/ this session.

## 1. WhiteNoiseSource — restored density/boost/continuity + new zeroCrossTendency (backlog 21)

`engine/include/mforce/source/white_noise_source.h`. The C# original
(`mforce-legacy/.../WhiteNoiseSource.cs`) carried Amplitude + Density + Boost +
Continuity as connectable inputs; the C++ port shipped `amplitude` only. Restored
the three, and ADDED `zeroCrossTendency` (Matt's ask — legacy WhiteNoise had none;
modelled on RedNoiseSource's sign rule) — all four as ValueSource pins with the
generic loader wiring them from the descriptors (no configurator needed).

Shaping logic mirrors legacy exactly: `if (decide(density)) v = range(boost,1)*sign;
if (continuity) v = range(lastVal, v, min(cont,.999)); else v = 0; v *= amplitude`,
with the sign flipped from the previous emission when `zeroCrossTendency` draws.

**Byte-identical defaults.** The legacy algorithm is a 3-draw construction
(decide + range + sign); the historical C++ output was a single `valuePN()` draw.
Those cannot both hold, and library/baselines patches (Piano_default, Piano_bright,
WIP*, the pluck baselines, mux_noise_test) all use WhiteNoise at defaults. So the
node keeps a fast path: at `density>=1, boost<=0, continuity<=0, zct<=0` it emits
exactly `valuePN() * amplitude` — one rng_ draw, same sequence as before. The
shaping path only engages when a param leaves its inert default (which changes the
draw structure by design). Defaults: density 1.0, boost 0.0, continuity 0.0,
zeroCrossTendency 0.0 — matches the legacy constructor and reproduces today's sound.

## 2. TriangleSource — `power` shape control

`engine/include/mforce/source/triangle_source.h`. New ValueSource `power`, modelled
on RampSource's Expo/Inverse_Expo (`MForce.Utility.Ramp`). Warps each of the two
straight legs:
- `|power| <= 1`: linear — the historical triangle, byte-for-byte (the neutral band
  returns the exact legacy arithmetic, not a re-derivation, so float rounding matches).
- `power > 1`: concave legs, `leg = t^power` (as Ramp Expo — slow start, fast finish).
- `power < -1`: convex legs, `leg = 1 - (1-t)^|power|` (mirror — fast start).

Signed control, neutral at 1 (default), per Matt's "positive = concave, negative =
convex, 1 = as now". Default 1.0 keeps every existing patch bit-identical.

## 3. UI piano — dynamic octaves + trailing top C

`tools/mforce_ui/main.cpp` (`draw_keyboard_panel`).
- (a) The keyboard now always ends on the C above the top octave: white-key count
  is `octaves*7 + 1`, drawn and click-tested via a flat index so the extra C is a
  real, playable key.
- (b) Octave count is chosen from the panel width to keep white-key width in a
  comfortable band instead of scaling without limit: default 4 octaves, add an
  octave when keys would exceed 40px, drop one below 20px (clamped 1..10 octaves).
  Widen the window and it grows an octave at a threshold; shrink it and it drops one.

Taste knobs (MIN_KEY_W 20 / MAX_KEY_W 40 / default 4 octaves) are single constants
at the top of the render block, easy for Matt to retune.

## Build + verification

- Engine headers changed → both targets rebuilt (Release). `mforce_cli` clean;
  `mforce_ui` relinked via rename-then-link (Matt's UI pid 6900 was up →
  `mforce_ui_locked_20260821.exe`). `mforce_ui.exe --stamp` exit 0, not stale.
- **Null gate 196/196 byte-identical** (`null_gate_perform_source.py`). The
  affected patches are all inside it and all passed: the WhiteNoise baselines
  (pluck_sanity, bend_test_pluck, bend_mordent_test_pluck, mux_noise_test, harsh_pluck),
  and all five TriangleSource baselines (TriTest, tri_test, inst_chord_test,
  MPXTest2, MPXTest3). The Piano patches that use WhiteNoise aren't in the frozen
  manifest, but they exercise the identical default fast path the baselines prove.
- **New behaviour engages** (patches/renders under `*/scratch/misc_0821/`):
  Triangle power 1 / 3 / -3 render three distinct hashes; sampled at a fixed
  phase on the rising leg the values order concave < linear < convex
  (-0.343 / -0.005 / +0.336), exactly the shape intended. WhiteNoise default vs
  {density .5, boost .6, continuity .7, zct .8} render distinct.

## Review queued

- **[listen] Triangle `power`** — the shape is a taste call. A/B pair prep is a
  scratch render today; queued in REVIEW as item 40 for Matt to confirm the
  concave/convex feel and the signed convention (vs. e.g. fractional-exponent).
- **[try] UI piano octaves + top C** — REVIEW 41. Resize the Keyboard panel and
  confirm the add/remove-octave thresholds and key-width band feel right, and that
  the trailing C plays.

## Follow-up (same session): Triangle shark-fin fixed → symmetric legs + `asymmetric` flag

Matt's verdict on the first Triangle cut: `power > 1` gave a "shark fin" — one
side convex, the other concave — because my falling leg was warped from its own
start (`1 - 2·warp(u)`) instead of mirroring the rising leg, so the two sides bent
in opposite senses. Fixed: the falling leg now mirrors the rising leg about the
peak (`-1 + 2·warp(1-u)`), so both sides bend the SAME way — a symmetric shape,
concave sides at `power>1` (pinched/spiky), convex at `power<-1` (domed/rounded).

The old per-leg math is preserved behind a new `asymmetric` Bool setting
(SettingType::Bool → renders as a checkbox in the UI automatically, loads
generically). Default off = symmetric.

Verified: `asymmetric=true` at power 3 renders BYTE-IDENTICAL to the first cut's
shark-fin (hash 0ee66bf3), so nothing was lost. Symmetric default at power 3
mirrors about the peak to within sample quantization (max leg asymmetry 0.014 vs
0.675 for the asymmetric arm). Null gate still 196/196 (power/asymmetric both at
inert defaults everywhere). Scratch: `renders/scratch/misc_0821/tri_pow3_{sym,asym}.wav`.

## Not done / notes

- No commit of scratch test patches (patches/scratch + renders/scratch are
  gitignored — derived data).
- WhiteNoise `zeroCrossTendency` is a NEW param beyond the legacy port; if Matt
  wants strict legacy fidelity it is inert at its 0.0 default and costs nothing.
