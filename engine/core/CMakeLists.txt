mforce_module(core
  SOURCES src/component.cpp
          src/source_registry.cpp)

mforce_tests(core
  SOURCES tests/main.cpp
          tests/alloc_probe_tests.cpp
          tests/driven_part_tests.cpp
          tests/dsp_primitive_tests.cpp
          tests/formant_factor_tests.cpp
          tests/pin_tests.cpp
          tests/ramp_hold_tests.cpp
          tests/segment_walk_tests.cpp
          tests/spsc_queue_tests.cpp
          tests/string_setting_tests.cpp)
