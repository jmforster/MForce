// The sound interfaces RtAudio was built with, read without opening a device.
#include "mforce/testing/check.h"
#include "mforce/ui_audio/audio_apis.h"
#include <algorithm>
#include <string>

void run_audio_api_tests() {
  const std::vector<std::string> apis = mforce::ui::compiled_audio_apis();
  CHECK(!apis.empty());
  CHECK(std::none_of(apis.begin(), apis.end(), [](const std::string& name) { return name.empty(); }));
#ifdef _WIN32
  CHECK(std::find(apis.begin(), apis.end(), "WASAPI") != apis.end());
#endif
}
