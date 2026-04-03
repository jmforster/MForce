add_library(mforce_engine
  src/red_noise_source.cpp
  src/wavetable_source.cpp
  src/full_additive_source.cpp
  src/additive_source2.cpp
  src/hybrid_ks_source.cpp
  src/music.cpp
  src/chord.cpp
  src/mixer.cpp
  src/wav_writer.cpp
  src/patch_loader.cpp
  src/source_registry.cpp
  src/source_registrations.cpp
)

target_include_directories(mforce_engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

if (MSVC)
  target_compile_options(mforce_engine PRIVATE /W4 /permissive-)
endif()
