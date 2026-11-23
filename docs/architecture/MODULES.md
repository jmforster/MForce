# Modules

Generated from `tools/structure/modules.json` by `tools/structure/check.py`.
Phase 1: describes today's layout. Do not edit by hand.

| Module | Purpose | Paths | May include |
|---|---|---|---|
| core | The ValueSource contract and the primitives every node shares; the node registry. | `engine/include/mforce/core/`, `engine/src/component.cpp`, `engine/src/source_registry.cpp` | nothing |
| contract | The types that cross from performance to realization. | `engine/include/mforce/contract/` | nothing |
| source | Every node type, one folder per family (filter folded in), and the list that registers them. | `engine/include/mforce/source/`, `engine/src/source/` | core |
| patch | A patch as data: the records, the declarations a type makes, the codec, the graph build. | `engine/include/mforce/patch/`, `engine/src/patch/` | core, source |
| render | Instruments, voices, mixing, a patch built as an instrument or a sound, WAV I/O. | `engine/include/mforce/render/`, `engine/src/render/` | core, source, patch, contract |
| music | The value types of music, and their codecs. | `engine/include/mforce/music/`, `engine/src/music/`, `engine/src/music.cpp`, `engine/src/chord.cpp` | core |
| voicing | How a chord symbol is voiced: the voicing selectors and profile selectors, their registry and codec. | `engine/include/mforce/voicing/` | core, music |
| compose | Composition: templates and their codecs, compose, the strategies, their registry and settings, the figure builders, walkers and profiles. | `engine/include/mforce/compose/`, `engine/src/compose/` | core, music, voicing |
| shelved | Strategies kept but not built on: registered through register_shelved only. | `engine/shelved/` | core, music, voicing, compose |
| perform | Performance: the conductor and performers, the time map, pitch gestures, the embedded score. | `engine/include/mforce/perform/`, `engine/src/perform/` | core, music, voicing, contract |
| play | One function that runs performance and realization end to end. | `engine/include/mforce/play/`, `engine/src/play/` | core, source, patch, render, music, perform, contract |
| util | Stand-alone analysis helpers (FFT, signal statistics). | `engine/include/mforce/util/` | nothing |
| cli | mforce_cli, the command-line renderer. | `tools/mforce_cli/` | core, source, patch, render, music, voicing, compose, shelved, perform, util, contract, play |
| tests | engine_tests and test_figures. | `tools/engine_tests/`, `tools/test_figures/`, `tools/alloc_probe/`, `tools/alloc_gate/`, `tools/alloc_gate.py`, `tools/tests/` | core, source, patch, render, music, voicing, compose, shelved, perform, util, contract, play |
| durn | durn_converter: notation formats to DURN. | `tools/durn_converter/` | music |
| misc_tools | ppl_to_json, stk_ref, wav_check. | `tools/shelved/ppl_to_json/`, `tools/stk_ref/`, `tools/wav_check.cpp`, `tools/wav_check.c` | core, source, patch, render, music, voicing, compose, shelved, perform, util, contract, play |

Third-party families: `json`, `imgui`, `rtaudio`, `rtmidi`, `glfw`.

`json` is allowed only in: `engine/include/mforce/patch/envelope_preset.h`, `engine/include/mforce/patch/patch_json.h`, `engine/src/patch/patch_json.cpp`, `engine/src/patch/node_json.cpp`, `engine/include/mforce/music/music_json.h`, `engine/include/mforce/voicing/voicing_json.h`, `engine/include/mforce/compose/templates_json.h`, `engine/include/mforce/compose/realization_strategy_json.h`, `engine/include/mforce/compose/melody_profile_json.h`, `engine/include/mforce/compose/pool_figure_builder_json.h`, `engine/src/compose/melody_profile_json.cpp`, `engine/src/compose/pool_figure_builder_json.cpp`, `engine/src/compose/style_table_json.cpp`, `engine/shelved/include/mforce/shelved/connective_passage_strategy_json.h`, `engine/shelved/include/mforce/shelved/elaborated_phrase_strategy_json.h`, `engine/shelved/include/mforce/shelved/library_passage_strategy_json.h`, `engine/shelved/include/mforce/shelved/pedal_buildup_strategy_json.h`, `engine/shelved/include/mforce/shelved/period_passage_strategy_json.h`, `engine/shelved/include/mforce/shelved/period_phrase_strategy_json.h`, `engine/shelved/include/mforce/shelved/sentence_phrase_strategy_json.h`, `engine/shelved/include/mforce/shelved/sequence_passage_strategy_json.h`, `engine/shelved/include/mforce/shelved/two_figure_phrase_strategy_json.h`, `engine/shelved/include/mforce/shelved/wandering_passage_strategy_json.h`, `engine/shelved/src/pattern_library_json.cpp`, `engine/src/perform/embedded_score.cpp`, `tools/`.
