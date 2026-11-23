# Modules

Generated from `tools/structure/modules.json` by `tools/structure/check.py`.
Do not edit by hand.

| Module | Purpose | Paths | May include |
|---|---|---|---|
| core | The ValueSource contract and the primitives every node shares; the node registry. | `engine/core/` | nothing |
| contract | The types that cross from performance to realization. | `engine/contract/` | nothing |
| source | Every node type, one folder per family (filter folded in), and the list that registers them. | `engine/source/` | core |
| patch | A patch as data: the records, the declarations a type makes, the codec, the graph build. | `engine/patch/` | core, source |
| render | Instruments, voices, mixing, a patch built as an instrument or a sound, WAV I/O. | `engine/render/` | core, source, patch, contract |
| music | The value types of music, and their codecs. | `engine/music/` | core |
| voicing | How a chord symbol is voiced: the voicing selectors and profile selectors, their registry and codec. | `engine/voicing/` | core, music |
| compose | Composition: templates and their codecs, compose, the strategies, their registry and settings, the figure builders, walkers and profiles. | `engine/compose/` | core, music, voicing |
| shelved | Strategies kept but not built on: registered through register_shelved only. | `engine/shelved/` | core, music, voicing, compose |
| perform | Performance: the conductor and performers, the time map, pitch gestures, the embedded score. | `engine/perform/` | core, music, voicing, contract |
| play | One function that runs performance and realization end to end. | `engine/play/` | core, source, patch, render, music, perform, contract |
| util | Stand-alone analysis helpers (FFT, signal statistics). | `engine/util/` | nothing |
| cli | mforce_cli, the command-line renderer. | `tools/mforce_cli/` | core, source, patch, render, music, voicing, compose, shelved, perform, util, contract, play |
| testing | The one test harness (check.h, run_tests) and the counting allocator. | `engine/testing/` | nothing |
| ui_model | The editor's model: the open patch, its file, whether it has unsaved changes. | `editor/ui_model/` | patch |
| ui_audio | The editor's sound output: the device, the callback, the player that sounds. | `editor/ui_audio/` | patch, render, contract, rtaudio |
| ui_control | The editor's state (Session), the edit functions the views call, live play. | `editor/ui_control/` | ui_model, ui_audio, patch, render, contract |
| ui_views | The editor's windows, one class each. | `editor/ui_views/` | ui_model, ui_control, patch, core, imgui |
| ui_app | mforce_editor: the window, the frame loop. | `editor/ui_app/` | ui_model, ui_audio, ui_control, ui_views, imgui, glfw |
| tests | The test programs (one per module's tests folder), the allocation gate and the tool tests. | `engine/core/tests/`, `engine/source/tests/`, `engine/patch/tests/`, `engine/render/tests/`, `engine/music/tests/`, `engine/voicing/tests/`, `engine/compose/tests/`, `engine/shelved/tests/`, `engine/perform/tests/`, `engine/play/tests/`, `engine/testing/tests/`, `editor/ui_model/tests/`, `editor/ui_audio/tests/`, `editor/ui_control/tests/`, `editor/ui_views/tests/`, `tools/alloc_gate/`, `tools/alloc_gate.py`, `tools/tests/` | core, source, patch, render, music, voicing, compose, shelved, perform, util, contract, play, testing, ui_model, ui_audio, ui_control, ui_views |
| durn | durn_converter: notation formats to DURN. | `tools/durn_converter/` | music |
| misc_tools | ppl_to_json, stk_ref, wav_check. | `tools/shelved/ppl_to_json/`, `tools/stk_ref/`, `tools/wav_check.cpp`, `tools/wav_check.c` | core, source, patch, render, music, voicing, compose, shelved, perform, util, contract, play |

Third-party families: `json`, `imgui`, `rtaudio`, `rtmidi`, `glfw`, `windows`.

`windows` is allowed only in: `editor/ui_app/`, `engine/testing/src/alloc_probe.cpp`, `tools/alloc_gate/main.cpp`.

`json` is allowed only in: `engine/patch/include/mforce/patch/envelope_preset.h`, `engine/patch/include/mforce/patch/patch_json.h`, `engine/patch/src/patch_json.cpp`, `engine/patch/src/node_json.cpp`, `engine/music/include/mforce/music/music_json.h`, `engine/voicing/include/mforce/voicing/voicing_json.h`, `engine/compose/include/mforce/compose/templates_json.h`, `engine/compose/include/mforce/compose/realization_strategy_json.h`, `engine/compose/include/mforce/compose/melody_profile_json.h`, `engine/compose/include/mforce/compose/pool_figure_builder_json.h`, `engine/compose/src/melody_profile_json.cpp`, `engine/compose/src/pool_figure_builder_json.cpp`, `engine/compose/src/style_table_json.cpp`, `engine/shelved/include/mforce/shelved/connective_passage_strategy_json.h`, `engine/shelved/include/mforce/shelved/elaborated_phrase_strategy_json.h`, `engine/shelved/include/mforce/shelved/library_passage_strategy_json.h`, `engine/shelved/include/mforce/shelved/pedal_buildup_strategy_json.h`, `engine/shelved/include/mforce/shelved/period_passage_strategy_json.h`, `engine/shelved/include/mforce/shelved/period_phrase_strategy_json.h`, `engine/shelved/include/mforce/shelved/sentence_phrase_strategy_json.h`, `engine/shelved/include/mforce/shelved/sequence_passage_strategy_json.h`, `engine/shelved/include/mforce/shelved/two_figure_phrase_strategy_json.h`, `engine/shelved/include/mforce/shelved/wandering_passage_strategy_json.h`, `engine/shelved/src/pattern_library_json.cpp`, `engine/perform/src/embedded_score.cpp`, `engine/core/tests/`, `engine/source/tests/`, `engine/patch/tests/`, `engine/render/tests/`, `engine/music/tests/`, `engine/voicing/tests/`, `engine/compose/tests/`, `engine/shelved/tests/`, `engine/perform/tests/`, `engine/play/tests/`, `tools/`.
