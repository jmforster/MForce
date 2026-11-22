add_library(mforce_engine
  src/source/noise/red_noise_source.cpp
  src/source/wavetable/wavetable_source.cpp
  src/source/additive/additive_source.cpp
  src/source/wavetable/hybrid_ks_source.cpp
  src/music.cpp
  src/chord.cpp
  src/render/mixer.cpp
  src/render/wav_writer.cpp
  src/render/wav_reader.cpp
  src/render/build_instrument.cpp
  src/render/build_sound.cpp
  src/render/instrument.cpp
  src/render/voice.cpp
  src/render/node_capture.cpp
  src/render/pitched_instrument.cpp
  src/render/drum_kit.cpp
  src/patch/build_graph.cpp
  src/patch/node_json.cpp
  src/patch/patch.cpp
  src/patch/patch_json.cpp
  src/patch/retired_spellings.cpp
  src/patch/table_rule.cpp
  src/patch/type_declarations.cpp
  src/music/embedded_score.cpp
  src/play/play.cpp
  src/component.cpp
  src/source_registry.cpp
  src/source/source_registrations.cpp
)

target_include_directories(mforce_engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if (MSVC)
  target_compile_options(mforce_engine PRIVATE /W4 /permissive-)
endif()
