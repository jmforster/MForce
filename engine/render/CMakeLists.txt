mforce_module(render
  SOURCES src/build_instrument.cpp
          src/build_sound.cpp
          src/drum_kit.cpp
          src/instrument.cpp
          src/live_player.cpp
          src/mixer.cpp
          src/node_capture.cpp
          src/pitched_instrument.cpp
          src/voice.cpp
          src/wav_reader.cpp
          src/wav_writer.cpp
  USES    core source patch contract)
