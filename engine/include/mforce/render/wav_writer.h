#pragma once
#include <string>
#include <vector>

namespace mforce {
bool write_wav_16le_stereo(const std::string& path, int sampleRate, const std::vector<float>& interleavedLR);
}
