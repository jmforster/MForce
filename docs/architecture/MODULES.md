# Modules

Generated from `tools/structure/modules.json` by `tools/structure/check.py`.
Phase 1: describes today's layout. Do not edit by hand.

| Module | Purpose | Paths | May include |
|---|---|---|---|
| core | The ValueSource contract and the primitives every node shares. | `engine/include/mforce/core/` | json |
| source | Nodes that produce sound (one layer with filter). | `engine/include/mforce/source/` | core, filter |
| filter | Nodes that process a signal (one layer with source). | `engine/include/mforce/filter/` | core, source |
| render | Instruments, voices, mixing, patch loading, WAV I/O. | `engine/include/mforce/render/`, `engine/src/patch_loader.cpp`, `engine/src/mixer.cpp`, `engine/src/wav_writer.cpp`, `engine/src/wav_reader.cpp`, `engine/src/source_registry.cpp`, `engine/src/source_registrations.cpp` | core, source, filter, json |
| music | The music model and composition. | `engine/include/mforce/music/`, `engine/src/music.cpp`, `engine/src/chord.cpp` | core, render, json |
| util | Stand-alone analysis helpers (FFT, signal statistics). | `engine/include/mforce/util/` | nothing |
| source_impl | Out-of-line bodies of source nodes. | `engine/src/red_noise_source.cpp`, `engine/src/wavetable_source.cpp`, `engine/src/full_additive_source.cpp`, `engine/src/additive_source2.cpp`, `engine/src/hybrid_ks_source.cpp` | core, source, filter |
| cli | mforce_cli, the command-line renderer. | `tools/mforce_cli/` | core, source, filter, render, music, util, json |
| ui | mforce_ui, the patch editor. | `tools/mforce_ui/` | core, source, filter, render, music, util, json, imgui, rtaudio, rtmidi, glfw |
| tests | engine_tests and test_figures. | `tools/engine_tests/`, `tools/test_figures/` | core, source, filter, render, music, util, json |
| durn | durn_converter: notation formats to DURN. | `tools/durn_converter/` | music, json |
| misc_tools | ppl_to_json, stamp_test, stk_ref, wav_check. | `tools/ppl_to_json/`, `tools/stamp_test/`, `tools/stk_ref/`, `tools/wav_check.cpp`, `tools/wav_check.c` | core, source, filter, render, music, util, json |

Third-party families: `json`, `imgui`, `rtaudio`, `rtmidi`, `glfw`
