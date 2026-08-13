# Note-contained sound — release inside duration, damper as envelope stage, key-up gating

Date: 2026-08-13 · Matt + Claude (interactive session) · dsp lane

## Why

The engine's original invariant — **all sound contained within the note's
duration** — is still how the entire additive lane works: `make_adsr` lays
Attack → Decay → Sustain → Release out across `durSamples`, release as the
final stage *inside* the note, and the envelope reaches 0 at the end (the
2026-04-20 peak-guard spec verified this explicitly).

The KS piano broke that. v6f, needing release *timing* for the damper and
finding no amplitude envelope on the string node to consult (a physical
model's shape is emergent — that's the point of it), invented a second,
MIDI-style convention bolted alongside the first:

- `RenderContext.noteOffFrame` at duration END, sound continuing after it;
- `instrument.release` (releaseSeconds) as a post-duration render window,
  widened to 2.0 s for the v6f2 damper A/B and never narrowed;
- a 0.5 s UI tail on top, and then the **silence-reclaim heuristic**
  (250 ms sniffer) to claw back the CPU the silent windows burned;
- the UI keyboard faking note-off at `keyDown + transportDuration`,
  key-up ignored — so a rolled chord fires a machine-gun of damper thuds
  no pianist could produce;
- a `Reverb` node inside the patch, which every CMA-ES run has scored
  *through* against close-mic'd Iowa references, with parameters the
  optimizer cannot touch.

Decisions from the 2026-08-13 session (Matt):

1. Release is **part of the note's duration** — the ADSR convention the
   engine already has. Anything to do with the end of the note belongs in
   the envelope-allotted release phase.
2. The damper thud fires at the **start** of the release phase and has
   fully died by duration end.
3. Internal (body) reflections may trail the last programmed sample, but
   only ~10 ms (piano-body reflections are 1–5 ms; Haas fusion boundary
   ~30 ms). A universal **10 ms allowance stolen from every note's
   duration** — unconditional, no reverb-node detection — restores "all
   sound within duration" as a hard property.
4. Short-note handling is **the patch designer's**, via the Stage
   semantics that already exist (pct of duration + min/max seconds) — no
   engine-imposed rule.
5. Room reverb does not belong in patches. Voice-shaping filters do.
   (Legacy precedent: no effects in patches; Unity layered them on.)
6. UI keyboard gets real gate semantics: **key-up = note-off**.
7. Silence reclaim goes away with the architecture that motivated it.
   The perf catastrophe it patched was denormals (fixed permanently by
   FTZ/DAZ) × guaranteed-silent 2.5 s windows (removed by this spec);
   neither returns. Live polyphony cost is a real but separate concern
   (additive low-note patches are heavy even monophonically) — if it ever
   needs work, the principled tool is a node *reporting* dead energy
   state, not an output sniffer. Not built here.

## What

### 1. KSPianoString: `damper` becomes a ValueSource input

New input param `damper`, 0 = open → 1 = fully damped, read per sample:

- effective per-comb feedback `g_eff = g · (1 − d · (1 − releaseFb))` —
  at d=0 identical to today's ringing string, at d=1 identical to today's
  post-note-off choke; continuous in between (**half-pedaling for free**).
- `damperNoise` thud: triggered when `d` rises across a small threshold
  (0.05), burst scaled by the string's ring level at that instant, as
  today. One trigger per rising crossing (re-arm when d falls back below
  threshold) — a re-dropped damper on a still-ringing string thuds again,
  which is physical.
- All `noteOffFrame` logic removed from the node.

### 2. Envelope: `damper` preset + gate-release operation

- New preset `damper`: two stages — hold 0.0 (expand-to-fill), then ramp
  0 → 1 as the **final stage**, with the standard Stage triple
  (`releasePct`, `releaseMin`, `releaseMax`). The damper's timing IS the
  last stage of an Envelope, same model as every amplitude ADSR. Ramp
  shape Linear (the choke physics supplies the actual decay curve).
  Short-note behavior is authored with the triple itself (Matt,
  2026-08-13): pct generous + max clamp = constant release on normal
  notes, proportional compression on short ones — no absolute-mode
  squeeze case for the damper.
- `timeMode` (absolute seconds, run-22 addition, currently JSON-only)
  gets **exposed in the UI on the Envelope node**.
- New operation `gate_release()` on Envelope (adsrLayout / damper
  presets): jump playback to the start of the final stage NOW, re-anchoring
  that stage's startVal to the envelope's current output so there is no
  click. Offline (scheduled) notes never call it; it exists for live
  gating (§4).

### 3. Voice lifetime = durSamples, exactly

- `instrument.release` (releaseSeconds) semantics DELETED from engine,
  loader, and UI. Voice/render length is `durSamples`, full stop.
  `prepare_voice` / `play_note` lose `relSamples`; StreamingVoice loses
  `releaseSamples`; the UI loses the 0.5 s tail, the damper fade, and the
  entire silence-reclaim plumbing (`silentRun`).
- **10 ms reflection allowance**: envelopes compile their stage layout
  over `durSamples − allowance` (constant, engine-wide). The programmed
  sound ends 10 ms early; the render ends at `durSamples`. Today nothing
  fills that window (the FIR early-reflection node is future work); the
  window exists so that when something does, the invariant already holds.
- **Assertable invariant**: CLI gains a post-render check — every note's
  output within its final 1 ms is ≤ −80 dBFS (ε = 1e-4; exponential
  decays never reach literal zero, so the invariant is defined at the
  audibility floor). Warns by name per note. The patch linter learns
  `instrument.release` is dead (hard finding, not silent ignore).

### 4. UI keyboard: key-up gating

- Key-down: prepare the voice with sustain held (existing
  `stream_envelopes_hold` semantics); pct-based A/D stages resolve
  against the transport duration as the nominal value.
- Key-up: `gate_release()` on every Envelope node in the voice's graph —
  amplitude envelopes and the damper envelope alike, one uniform
  mechanism for every patch type. Voice ends `longestReleaseStage +
  allowance` after key-up.
- A rolled chord released together now produces one collective damper
  event. The transport duration knob stays as the nominal for pct
  resolution and for click-to-play (mouse clicks get scheduled notes as
  today).

### 5. Patches: v6m reconstruction + verb removal

`v6m` = `v6l_cmaes3` restated under this model:

- `verb` node removed; `string` connects to output. (Its dry path was
  0.85, so fold ×0.85 into the calibrated volume to preserve level.)
- New `env_damper` node (preset `damper`, releasePct 0.5 / releaseMax
  0.25 s — constant quarter-second damper on notes ≥ 0.5 s, half the
  note below; Matt's formulation 2026-08-13) wired to `string.damper`.
- `instrument.release` key removed.
- Everything else byte-identical to v6l.

Room reverb as a master-bus / listening-chain feature is a follow-up,
not this spec. The two **library keepers** containing Reverb nodes
(`cello_full_range`, `funky_recorder`) are locked approved sounds — left
untouched; migrating them is Matt's call, separately.

## Non-goals

- Master-bus effects chain (separate feature, needed before renders stop
  sounding dry — sequencing with Matt).
- FIR EarlyReflections node (≤10 ms taps, bounded by construction —
  reserved; the allowance is sized for it).
- Node-level "energy dead" voice-end reporting (only if live polyphony
  measures as a problem after this lands).
- Comp-lane articulation (Performer authoring gate < notated duration) —
  *enabled* by this model, implemented in the comp lane when wanted.
- Any change to stream mode (streams have no duration; envelopes already
  hold).

## Verification

Ordered so refactor correctness is proven separately from intentional
sound changes:

1. Build both targets; `mforce_ui --stamp` exit 0.
2. **Null partition at allowance = 0**: with the allowance temporarily 0,
   every patch with `instrument.release` absent-or-0 renders
   byte-identical (that is every patch outside the ks_piano family —
   measured 2026-08-13: nonzero release exists in exactly 54 files, all
   `pending/sweep` ks_piano). Proves the releaseSeconds/noteOffFrame
   removal touched nothing else.
3. **Flip allowance to 10 ms**: every enveloped patch's stage layout
   shifts by 10 ms of sustain — an intentional, sub-audible,
   corpus-wide change (no-backcompat policy applies). Spot-measure two
   patches: envelope reaches 0 at durSamples − 10 ms exactly.
4. **v6m against v6l, no ears needed for correctness**: render
   `v6l_null` (v6l with wet 0, dry 1.0) and `v6m` on the same score.
   Same graph, same seed ⇒ samples over `[0, dur − 0.25 s − 10 ms)`
   must be byte-identical (scaled by the folded 0.85); they may differ
   only in the release region, where v6m dampens early by design and
   v6l_null rings to the cut. Automated range compare. Ears then judge
   taste (verb gone, earlier damper), not correctness.
5. **Invariant check live**: CLI post-render check passes on v6m across
   the full 87-note sweep (tools/measure_ks_v6_levels.py reused);
   deliberately break it (releaseMin 0.01) and confirm it fails.
6. **Broken-chord test** (manual, UI): roll a chord, release together —
   one damper event. Hold a key past the transport duration — note
   sustains until key-up.
7. Re-render the v6 audition A/B set for Matt's ears; re-run the CMA-ES
   scorer on v6m to re-baseline the objective without the verb (Iowa
   references unchanged).
8. Perf: confirm no glitch at a 10-voice stack on v6m in the UI (the
   dc7a9dd fixes 1–2 stay; expected silent-voice cost ~4.5%/core each).

## Blast radius (measured, 2026-08-13)

- `instrument.release ≠ 0`: **54 patches, all ks_piano pending/sweep** —
  0 library, 0 baselines. The v6 audition set gets re-rendered anyway;
  v5/sweep families are superseded material.
- Reverb nodes: same family + `_fx_reverb_test` (the node's own
  baseline, kept) + old/ + the 2 library keepers (untouched, flagged).
- Every enveloped patch: 10 ms layout shift (intentional, step 3).
- UI live behavior: gate semantics change for every patch (the feature).
- CMA-ES objective: re-baselined by verb removal — run3's 0.5111 is not
  comparable to post-change scores; noted so nobody reads a regression.

## Open questions — RESOLVED (Matt, 2026-08-13)

1. Damper thud re-triggers on each rising crossing (re-arm). The choke
   happens regardless; suppressing the noise would be the noiseless-fade
   artifact again.
2. v6m damper stage: percent mode, releasePct 0.5 / releaseMax 0.25 —
   designer-authored short-note compression via the Stage triple.
3. CLI containment check WARNS (by note name), never hard-fails — a
   containment miss is a patch-design finding, and a hard fail would
   kill optimizer runs on long-ringing candidates instead of scoring
   them badly.
