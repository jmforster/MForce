mforce_module(ui_audio
  SOURCES src/audio_adapter.cpp
          src/audio_device.cpp
          src/sound_line.cpp
  USES    render
  LIBS    rtaudio)

mforce_tests(ui_audio
  SOURCES tests/main.cpp
          tests/audio_adapter_tests.cpp)
