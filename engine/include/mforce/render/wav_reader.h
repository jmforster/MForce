#pragma once
#include <string>
#include <vector>

namespace mforce {

// Read a WAV file written by write_wav_16le_stereo (16-bit PCM, stereo).
// Returns interleaved L/R float samples in [-1, 1] via out, sampleRate via
// outSampleRate. Returns false on any parse error / unsupported format.
//
// Intentionally narrow: just enough to read the audition material that
// mforce_cli --explore produces. A general WAV parser would handle 24/32-bit
// PCM, float WAVE_FORMAT_EXTENSIBLE, mono, etc.
bool read_wav_16le_stereo(const std::string& path,
                          std::vector<float>& out,
                          int& outSampleRate);

} // namespace mforce
