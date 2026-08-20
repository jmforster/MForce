# Pin / config census — every node type, every setting

Generated from `mforce_cli --dump-descriptors` (the registry itself, not a
hand-copied list) plus a scan of every patch under `patches/`. Regenerate
with the script in the session scratchpad; it takes the descriptor dump as
its one argument.

Purpose: settle which settings could ever be driven per note, so the
config-chain design has a real inventory instead of two examples.

## Totals

| | count |
|---|---|
| registered node types | 76 |
| params (pins today) | 189 |
| inputs (pins today) | 30 |
| **float configs** | **212** |
| int configs (non-enum) | 21 |
| bool configs | 11 |
| enum configs | 2 |
| distinct (type, target) pairs actually mapped in patches/ | 34 |

## What the patch tree actually drives per note

Every distinct target across every patch, by how many patches use it.
This is the demand that exists today — 34 pairs out of 465 possible.

| node type | target | what it is now | patches |
|---|---|---|---|
| AdditiveSource | `frequency` | pin (param) | 230 |
| HammerBank | `frequency` | pin (param) | 147 |
| BWLowpassFilter | `cutoffFreq` | pin (param) | 145 |
| CombinedSource | `source2` | pin (input) | 138 |
| KSPianoString | `frequency` | pin (param) | 85 |
| KSPianoString | `t60` | config (float) | 85 |
| KSPianoString | `brightness` | config (float) | 78 |
| KSPianoString | `dispersion` | config (float) | 68 |
| KSPianoString | `inharmGain` | config (float) | 68 |
| SVFSource | `cutoffFreq` | pin (param) | 61 |
| Vibrato | `frequency` | pin (param) | 47 |
| KSPianoString | `detune` | config (float) | 21 |
| WavetableSource | `frequency` | pin (param) | 18 |
| FMSource | `frequency` | pin (param) | 18 |
| Formant | `frequency` | pin (param) | 18 |
| Envelope | `timeScale` | config (float) | 13 |
| SawSource | `frequency` | pin (param) | 6 |
| AdditiveSource | `noiseBedDelay` | config (float) | 5 |
| AdditiveSource | `noiseBedFade` | config (float) | 5 |
| HammerBank | `resStart` | config (float) | 5 |
| VarSource | `val` | pin (param) | 4 |
| Envelope | `sustainLevel` | config (float) | 4 |
| SineSource | `frequency` | pin (param) | 3 |
| TriangleSource | `frequency` | pin (param) | 2 |
| Envelope | `stage_accuracy` | config (float) | 2 |
| ExplicitPartials | `inharmonicity` | config (float) | 2 |
| RedNoiseSource | `frequency` | pin (param) | 1 |
| ExplicitPartials | `bandwidth1` | config (float) | 1 |
| AllpassResonator | `frequency` | pin (param) | 1 |
| AllpassResonator | `feedback` | config (float) | 1 |
| AllpassResonator | `damping` | config (float) | 1 |
| AllpassResonator | `stiffness` | config (float) | 1 |
| ExplicitPartials | `decayRate` | config (float) | 1 |
| ExplicitPartials | `rolloff1` | config (float) | 1 |

## Every type, every setting

Sorted by float-config count — the types at the top are the ones where
'give every candidate a pin' would crowd the node face.

| node type | pins today (params + inputs) | float configs | structural configs (int/bool/enum) | arrays |
|---|---|---|---|---|
| **FullPartials** | `multEnv`, `amplEnv`, `poEnv`, `roEnv`, `dtEnv`, `bwEnv`, `motionEnv`, `shimmerEnv` | **33:** `evenWeight1`, `evenWeight2`, `oddWeight1`, `oddWeight2`, `unitPO1`, `unitPO2`, `rolloff1`, `rolloff2`, `detune1`, `detune2`, `bandwidth1`, `bandwidth2`, `bandwidthHz`, `motionDepth1`, `motionDepth2`, `motionHz`, `motionCoherence`, `motionEvolve`, `motionScale`, `shimmerDepth1`, `shimmerDepth2`, `shimmerHz`, `shimmerCoherence`, `shimmerEvolve`, `shimmerFloor`, `tradeDepth`, `tradeHz`, `onsetSpread`, `onsetTilt`, `onsetFade`, `decayRate`, `decayExp`, `inharmonicity` | `maxPartials (int)`, `minMult (int)` | — |
| **SequencePartials** | `multEnv`, `amplEnv`, `poEnv`, `roEnv`, `dtEnv`, `bwEnv`, `motionEnv`, `shimmerEnv` | **33:** `minMult1`, `minMult2`, `incr1`, `incr2`, `unitPO1`, `unitPO2`, `rolloff1`, `rolloff2`, `detune1`, `detune2`, `bandwidth1`, `bandwidth2`, `bandwidthHz`, `motionDepth1`, `motionDepth2`, `motionHz`, `motionCoherence`, `motionEvolve`, `motionScale`, `shimmerDepth1`, `shimmerDepth2`, `shimmerHz`, `shimmerCoherence`, `shimmerEvolve`, `shimmerFloor`, `tradeDepth`, `tradeHz`, `onsetSpread`, `onsetTilt`, `onsetFade`, `decayRate`, `decayExp`, `inharmonicity` | `maxPartials (int)` | — |
| **ExplicitPartials** | `multEnv`, `amplEnv`, `poEnv`, `roEnv`, `dtEnv`, `bwEnv`, `motionEnv`, `shimmerEnv` | **29:** `unitPO1`, `unitPO2`, `rolloff1`, `rolloff2`, `detune1`, `detune2`, `bandwidth1`, `bandwidth2`, `bandwidthHz`, `motionDepth1`, `motionDepth2`, `motionHz`, `motionCoherence`, `motionEvolve`, `motionScale`, `shimmerDepth1`, `shimmerDepth2`, `shimmerHz`, `shimmerCoherence`, `shimmerEvolve`, `shimmerFloor`, `tradeDepth`, `tradeHz`, `onsetSpread`, `onsetTilt`, `onsetFade`, `decayRate`, `decayExp`, `inharmonicity` | `maxPartials (int)`, `evolve (bool)` | `mult1`, `ampl1`, `mult2`, `ampl2` |
| **KSPianoString** | `frequency`, `amplitude`, `source`, `damper` | **15:** `detune`, `t60`, `brightness`, `dispersion`, `inharmGain`, `inharmFb`, `inharmHp`, `ap1`, `ap2`, `ap3`, `fbCoeff`, `releaseFb`, `damperNoise`, `exciteGain`, `direct` | `numCombs (int)` | — |
| **HammerBank** | `frequency`, `source` | **10:** `harm1`, `harm2`, `harm3`, `harm4`, `resStart`, `resEnd`, `resDecay`, `bandTilt`, `direct`, `gain` | `numBands (int)` | — |
| **Vibrato** | `frequency` | **7:** `speed`, `depth`, `attack`, `threshold`, `speedVar`, `depthVar`, `zeroCrossTendency` | — | — |
| **ADSREnvelope** | `minValue`, `maxValue` | **6:** `attack`, `decay`, `sustainLevel`, `release`, `stage_accuracy`, `ramp_accuracy` | — | — |
| **AdditiveSource** | `frequency`, `amplitude`, `phase`, `formantWeight`, `formantFloor`, `formant`, `partials` | **6:** `noiseBedLevel`, `noiseBedFreq`, `noiseBedWidth`, `noiseBedDelay`, `noiseBedFadePow`, `noiseBedFade` | — | — |
| **AllpassResonator** | `frequency`, `amplitude`, `source` | **6:** `feedback`, `damping`, `stiffness`, `tension`, `exciteGain`, `direct` | — | — |
| **ADREnvelope** | `minValue`, `maxValue` | **5:** `attack`, `decay`, `decayLevel`, `stage_accuracy`, `ramp_accuracy` | — | — |
| **ADSEnvelope** | `minValue`, `maxValue` | **5:** `attack`, `decay`, `sustainLevel`, `stage_accuracy`, `ramp_accuracy` | — | — |
| **ASREnvelope** | `minValue`, `maxValue` | **5:** `attack`, `sustainLevel`, `release`, `stage_accuracy`, `ramp_accuracy` | — | — |
| **BezierPullEvolution** | — | **5:** `p0`, `p1`, `p2`, `p3`, `rate` | — | — |
| **EKSEvolution** | — | **5:** `pickPosition`, `pickDirection`, `stiffness`, `decayStretch`, `drumBlend` | — | — |
| **ReactionDiffusionEvolution** | — | **5:** `feed`, `kill`, `diffU`, `diffV`, `dt` | `subSteps (int)` | — |
| **ASEnvelope** | `minValue`, `maxValue` | `attack`, `sustainLevel`, `stage_accuracy`, `ramp_accuracy` | `reverse (bool)` | — |
| **BrassEvolution** | `brassiness`, `breath` | `tubeLoss`, `lipTension`, `lipFreqRatio`, `lipQ` | — | — |
| **Envelope** | `minValue`, `maxValue` | `stage_accuracy`, `ramp_accuracy`, `sustainLevel`, `timeScale` | — | — |
| **RepeatingSource** | `source` | `duration`, `durVarPct`, `gapDuration`, `gapVarPct` | — | — |
| **AREnvelope** | `minValue`, `maxValue` | `attack`, `stage_accuracy`, `ramp_accuracy` | — | — |
| **AveragingEvolution** | — | `sampleCount`, `speed`, `decayFactor` | `leading (bool)`, `autoAdjust (bool)` | — |
| **BowedStringEvolution** | `frictionGain`, `bow` | `tubeLoss`, `bowSpeed`, `bowPosition` | — | — |
| **CellularAutomatonEvolution** | — | `threshold`, `levelHi`, `levelLo` | `rule (int)`, `stepEvery (int)` | — |
| **CrossfadeSource** | `amplitude`, `source1`, `source2` | `ratio`, `overlap`, `gainAdj` | — | — |
| **ReedEvolution** | `reedStiffness`, `breath` | `tubeLoss`, `loopFilter` | — | — |
| **CombinedSource** | `source1`, `source2` | `gainAdj` | `operation (int, enum)` | — |
| **HistogramEqualizeEvolution** | — | `rate` | `bins (int)` | — |
| **HybridKSSource** | `frequency`, `amplitude`, `phase`, `inputSource` | `morphDuration` | `holdCycles (int)`, `numPartials (int)` | — |
| **PluckEvolution** | — | `muting` | — | — |
| **AdditiveSource2** | `frequency`, `amplitude`, `phase`, `phaseOffset`, `freqVarDepth`, `freqVarSpeed`, `amplVarDepth`, `amplVarSpeed`, `partials` | — | `partialCount (int)` | — |
| **BWBandpassFilter** | `lowCutoff`, `highCutoff`, `source` | — | — | — |
| **BWHighpassFilter** | `cutoffFreq`, `source` | — | — | — |
| **BWLowpassFilter** | `cutoffFreq`, `source` | — | — | — |
| **BandSpectrum** | `startFreq`, `freqIncrement` | — | — | `gains` |
| **BasicAdditiveSource** | `frequency`, `amplitude`, `phase`, `evenWeight`, `oddWeight`, `rolloff`, `freqVarPct`, `freqVarSpeed`, `amplVarPct`, `amplVarSpeed` | — | — | — |
| **BitRotateEvolution** | — | — | `shiftBits (int)`, `stepEvery (int)` | — |
| **BlueNoiseSource** | `amplitude` | — | — | — |
| **CompositePartials** | `partials` | — | — | — |
| **CrackleNoiseSource** | `chaos` | — | — | — |
| **CurveNode** | `source` | — | — | — |
| **DelayFilter** | `delayTime`, `delayLevel`, `feedback`, `source` | — | — | — |
| **DistortedSource** | `amplitude`, `density`, `gain`, `shift`, `source` | — | — | — |
| **ExpandRule** | `spacing1`, `spacing2`, `dt1`, `dt2`, `loPct1`, `loPct2`, `power1`, `power2`, `po1`, `po2` | — | `count (int)`, `recurse (int)` | — |
| **FMSource** | `frequency`, `amplitude`, `phase`, `carrierRatio`, `modRatio`, `depth` | — | `unbounded_pos (bool)`, `oversample (int)` | — |
| **FixedSpectrum** | — | — | — | `gains` |
| **Formant** | `frequency`, `gain`, `width`, `power` | — | — | — |
| **FormantSequence** | `blend`, `spectra` | — | — | — |
| **FormantSpectrum** | — | — | — | — |
| **LayeredRedNoiseSource** | — | — | `count (int)` | `frequency`, `amplitude` |
| **Limiter** | `threshold`, `release`, `source` | — | — | — |
| **MultiSource** | `source` | — | — | — |
| **MultiplexSource** | `source` | — | `count (int)` | — |
| **MurmurationNoiseSource** | `count`, `cohesion`, `alignment`, `separation`, `chaos`, `speed` | — | — | — |
| **PerlinNoiseSource** | `speed`, `octaves`, `persistence`, `lacunarity` | — | — | — |
| **PhasedValueSource** | `amplitude` | — | — | — |
| **PinkNoiseSource** | `amplitude` | — | — | — |
| **PulseSource** | `frequency`, `amplitude`, `phase`, `dutyCycle`, `bend` | — | — | — |
| **RangeSource** | `min`, `max`, `var` | — | `normalized (bool)` | — |
| **RedNoiseSource** | `frequency`, `amplitude`, `phase`, `density`, `smoothness`, `rampVariation`, `boost`, `continuity`, `zeroCrossTendency` | — | — | — |
| **Reverb** | `roomSize`, `damping`, `wet`, `dry`, `source` | — | — | — |
| **SVFSource** | `cutoffFreq`, `resonance`, `source` | — | `mode (int, enum)`, `normalize (bool)` | — |
| **SawSource** | `frequency`, `amplitude`, `phase` | — | — | — |
| **SegmentSource** | `amplitude`, `smoothness`, `widthVarPct`, `valVarPct`, `gap`, `gapVarPct` | — | `oneShot (bool)` | `values` |
| **SineSource** | `frequency`, `amplitude`, `phase` | — | — | — |
| **SortErosionEvolution** | — | — | `swapsPerSample (int)`, `descending (bool)` | — |
| **StaticRangeSource** | — | — | — | — |
| **StaticVarSource** | — | — | — | — |
| **TriangleSource** | `frequency`, `amplitude`, `phase`, `bias` | — | — | — |
| **VarSource** | `val`, `var`, `varPct` | — | `absolute (bool)` | — |
| **VelvetNoiseSource** | `density`, `amplitude` | — | — | — |
| **VioletNoiseSource** | `amplitude` | — | — | — |
| **WanderNoise2Source** | `amplitude`, `minSpeed`, `maxSpeed`, `reverseProb`, `retraceProb`, `retracePct` | — | — | — |
| **WanderNoise3Source** | `amplitude`, `speed`, `deltaSpeed`, `slopeLimit` | — | — | — |
| **WanderNoiseSource** | `amplitude`, `speed`, `deltaSpeed`, `slopeLimit` | — | — | — |
| **WavetableSource** | `frequency`, `amplitude`, `phase`, `speedFactor`, `inputSource`, `evolution` | — | `interpolate (bool)` | — |
| **WhiteNoiseSource** | `amplitude` | — | — | — |

---

## Promotion eligibility — audit (2026-08-19)

**Decision (Matt): use data type for now. If it's a float, it's promotable.**
If a non-promotable-float pattern emerges during patch dev, revisit then. No
separate eligibility flag on `ConfigDescriptor` until something forces one.

The audit behind that decision. The question was whether any **float** config
does something in `set_config` that per-note pushing could not survive —
allocation, resizing, structural change. All 43 `set_config` bodies in
`engine/` were parsed and their call graphs followed one level.

**Result: no counter-example. Everything that allocates is int-triggered.**

| allocating call | triggered by | type |
|---|---|---|
| `resize_arrays_to_count_()` | `LayeredRedNoiseSource.count` | int |
| `set_default_partials()` | `AdditiveSource2.partialCount` | int |
| (buffer sizing) | `numBands`, `numCombs`, `maxPartials`, `subSteps`, `bins` | int |

Float configs that rebuild but do **not** allocate:

- `PluckEvolution.muting` → `evo_ = PluckEvolution(muting_, seed_)`. Ctor is
  `muting_(muting), rng_(seed)` — member init only.
- `AveragingEvolution.sampleCount` / `speed` / `decayFactor` → `rebuild()`,
  same story: the ctor is pure member init.
- `Partials` float configs (`rolloff*`, `detune*`, `evenWeight*`, …) set a
  dirty flag; the array rebuild happens later in `update_arrays()`, not inside
  `set_config`.

**The one case that is not provably allocation-free:** the envelope presets.
`AREnvelope.attack`, `ASEnvelope.attack`/`sustainLevel` and siblings call
`rebuild()`, which does `*static_cast<Envelope*>(this) = Envelope::make_ar(...)`
— a whole-object assignment carrying a `std::vector<Stage>`. Stage count is
identical every time, so in steady state the vector reuses its capacity and no
allocation occurs; but that is an implementation property, not a guarantee.

Context for why that is acceptable: note-on is not the render loop.
`apply_note_bindings` runs before `vg.source->prepare`, on whichever thread
triggered the note — so a stray allocation there would surface as live-playback
jitter, not as an audio-callback underrun, and does not breach the
no-allocation-in-hot-loops rule.

## Settled in the 2026-08-19 discussion

- **Pins are a property of the patch, not the type.** A config has no pin until
  something drives it. This is what makes the 212-vs-34 gap survivable:
  FullPartials does not wear 33 pins, it wears the ones that patch uses.
- **Promotion is a UI act** (Matt's design): a grey pin sits beside every
  eligible config in the Settings pane. Click it — it turns gold, the data-entry
  widget is replaced by a label, and the pin appears in the node's "dynamic pin"
  section. Precedent exists: `mapping_badge` (`tools/mforce_ui/main.cpp:6114`)
  already swaps the widget for a `<curve>` label when something drives it.
- **Two pin flavours:** the fixed set (ValueSource params/inputs, pulled) and the
  dynamic set (promoted float configs, pushed).
- **A dynamic pin's value is set once per note, by definition.** Not incidental
  — the contract. Spec §6.6 already parks a per-block relaxation (`t60` tracking
  a bend), so that stays a named future rather than an accident.
- **OPEN:** what may feed a dynamic pin. Today it is only ever a curve off the
  note. Wiring an LFO in would sample it once at note-on — well defined, and
  surprising. Matt: "it might be that Curve is the *only* thing these guys can
  get." Not decided; revisit before anything can author it.
- **OPEN:** terminology. "config" is a port invention with no legacy ancestor;
  a rename touches 2 virtuals, 1 struct, 1 enum, 27 files, and zero patch files
  or user-facing strings.
