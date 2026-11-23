#include "mforce/ui_audio/audio_apis.h"
#include <RtAudio.h>

namespace mforce::ui {

std::vector<std::string> compiled_audio_apis() {
  std::vector<RtAudio::Api> apis;
  RtAudio::getCompiledApi(apis);
  std::vector<std::string> names;
  names.reserve(apis.size());
  for (RtAudio::Api api : apis)
    names.push_back(RtAudio::getApiDisplayName(api));
  return names;
}

}  // namespace mforce::ui
