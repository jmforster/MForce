# Modules

Generated from `tools/structure/modules.json` by `tools/structure/check.py`.
Phase 1: describes today's layout. Do not edit by hand.

| Module | Purpose | Paths | May include |
|---|---|---|---|
| core | The ValueSource contract and the primitives every node shares; the node registry. | `engine/include/mforce/core/`, `engine/src/component.cpp`, `engine/src/source_registry.cpp` | nothing |
| contract | The types that cross from performance to realization. | `engine/include/mforce/contract/` | nothing |
| source | Every node type, one folder per family (filter folded in), and the list that registers them. | `engine/include/mforce/source/`, `engine/src/source/` | core |
| patch | A patch as data: the records, the declarations a type makes, the codec, the graph build. | `engine/include/mforce/patch/`, `engine/src/patch/` | core, source |
| render | Instruments, voices, mixing, a patch built as an instrument or a sound, WAV I/O. | `engine/include/mforce/render/`, `engine/src/build_instrument.cpp`, `engine/src/build_sound.cpp`, `engine/src/embedded_score.cpp`, `engine/src/mixer.cpp`, `engine/src/wav_writer.cpp`, `engine/src/wav_reader.cpp` | core, source, patch, contract |
| music | The music model and composition. | `engine/include/mforce/music/`, `engine/src/music.cpp`, `engine/src/chord.cpp` | core, render, contract |
| util | Stand-alone analysis helpers (FFT, signal statistics). | `engine/include/mforce/util/` | nothing |
| cli | mforce_cli, the command-line renderer. | `tools/mforce_cli/` | core, source, patch, render, music, util, contract |
| tests | engine_tests and test_figures. | `tools/engine_tests/`, `tools/test_figures/`, `tools/alloc_probe/`, `tools/alloc_gate/`, `tools/alloc_gate.py`, `tools/tests/` | core, source, patch, render, music, util, contract |
| durn | durn_converter: notation formats to DURN. | `tools/durn_converter/` | music |
| misc_tools | ppl_to_json, stk_ref, wav_check. | `tools/ppl_to_json/`, `tools/stk_ref/`, `tools/wav_check.cpp`, `tools/wav_check.c` | core, source, patch, render, music, util, contract |

Third-party families: `json`, `imgui`, `rtaudio`, `rtmidi`, `glfw`.

`json` is allowed only in: `engine/include/mforce/patch/envelope_preset.h`, `engine/include/mforce/patch/patch_json.h`, `engine/src/patch/patch_json.cpp`, `engine/src/patch/node_json.cpp`, `engine/include/mforce/music/music_json.h`, `engine/include/mforce/music/templates_json.h`, `engine/src/embedded_score.cpp`, `tools/`.
