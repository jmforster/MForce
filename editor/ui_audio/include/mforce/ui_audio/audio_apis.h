#pragma once
#include <string>
#include <vector>

namespace mforce::ui {

// The sound interfaces this build can open a device through (WASAPI on
// Windows), by the names RtAudio shows for them. Opens no device.
std::vector<std::string> compiled_audio_apis();

}  // namespace mforce::ui
