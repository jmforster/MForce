# Modules

Generated from `tools/structure/modules.json` by `tools/structure/check.py`.
Phase 1: describes today's layout. Do not edit by hand.

| Module | Purpose | Paths | May include |
|---|---|---|---|
| core | The ValueSource contract and the primitives every node shares; the node registry. | `engine/include/mforce/core/`, `engine/src/component.cpp`, `engine/src/source_registry.cpp` | nothing |
| source | Every node type, one folder per family (filter folded in), and the list that registers them. | `engine/include/mforce/source/`, `engine/src/source/` | core |
| render | Instruments, voices, mixing, patch loading, WAV I/O. | `engine/include/mforce/render/`, `engine/src/patch_loader.cpp`, `engine/src/mixer.cpp`, `engine/src/wav_writer.cpp`, `engine/src/wav_reader.cpp` | core, source |
| music | The music model and composition. | `engine/include/mforce/music/`, `engine/src/music.cpp`, `engine/src/chord.cpp` | core, render |
| util | Stand-alone analysis helpers (FFT, signal statistics). | `engine/include/mforce/util/` | nothing |
| cli | mforce_cli, the command-line renderer. | `tools/mforce_cli/` | core, source, render, music, util |
| ui | mforce_ui, the patch editor. | `tools/mforce_ui/` | core, source, render, music, util, imgui, rtaudio, rtmidi, glfw |
| tests | engine_tests and test_figures. | `tools/engine_tests/`, `tools/test_figures/`, `tools/alloc_probe/`, `tools/alloc_gate/`, `tools/alloc_gate.py`, `tools/tests/` | core, source, render, music, util |
| durn | durn_converter: notation formats to DURN. | `tools/durn_converter/` | music |
| misc_tools | ppl_to_json, stamp_test, stk_ref, wav_check. | `tools/ppl_to_json/`, `tools/stamp_test/`, `tools/stk_ref/`, `tools/wav_check.cpp`, `tools/wav_check.c` | core, source, render, music, util |

Third-party families: `json`, `imgui`, `rtaudio`, `rtmidi`, `glfw`.

`json` is allowed only in: `engine/include/mforce/source/envelope/envelope_json.h`, `engine/include/mforce/core/source_registry.h`, `engine/include/mforce/music/music_json.h`, `engine/include/mforce/music/templates_json.h`, `engine/src/patch_loader.cpp`, `tools/`.
