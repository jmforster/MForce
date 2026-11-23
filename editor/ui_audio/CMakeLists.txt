mforce_module(ui_audio
  SOURCES src/audio_adapter.cpp
          src/audio_device.cpp
  USES    patch render contract
  LIBS    rtaudio)

mforce_tests(ui_audio
  SOURCES tests/main.cpp
          tests/audio_adapter_tests.cpp)
