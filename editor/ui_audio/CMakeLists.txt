mforce_module(ui_audio
  SOURCES src/audio_apis.cpp
  USES    patch render contract
  LIBS    rtaudio)

mforce_tests(ui_audio
  SOURCES tests/main.cpp
          tests/audio_apis_tests.cpp)
