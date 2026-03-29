#include "mforce/wav_writer.h"
#include <cstdint>
#include <fstream>
#include <algorithm>

namespace mforce {

static void write_u32(std::ofstream& f, uint32_t v) {
  f.put(char(v & 0xFF));
  f.put(char((v >> 8) & 0xFF));
  f.put(char((v >> 16) & 0xFF));
  f.put(char((v >> 24) & 0xFF));
}

static void write_u16(std::ofstream& f, uint16_t v) {
  f.put(char(v & 0xFF));
  f.put(char((v >> 8) & 0xFF));
}

bool write_wav_16le_stereo(const std::string& path, int sampleRate, const std::vector<float>& interleavedLR) {
  if (interleavedLR.size() % 2 != 0) return false;

  const uint16_t numChannels = 2;
  const uint16_t bitsPerSample = 16;
  const uint16_t blockAlign = numChannels * (bitsPerSample / 8);
  const uint32_t byteRate = uint32_t(sampleRate) * blockAlign;
  const uint32_t dataBytes = uint32_t(interleavedLR.size() / 2) * blockAlign;

  std::ofstream f(path, std::ios::binary);
  if (!f) return false;

  // RIFF header
  f.write("RIFF", 4);
  write_u32(f, 36 + dataBytes);
  f.write("WAVE", 4);

  // fmt chunk
  f.write("fmt ", 4);
  write_u32(f, 16);
  write_u16(f, 1); // PCM
  write_u16(f, numChannels);
  write_u32(f, uint32_t(sampleRate));
  write_u32(f, byteRate);
  write_u16(f, blockAlign);
  write_u16(f, bitsPerSample);

  // data chunk
  f.write("data", 4);
  write_u32(f, dataBytes);

  auto clip = [](float x) {
    x = std::clamp(x, -1.0f, 1.0f);
    return int16_t(x < 0 ? x * 32768.0f : x * 32767.0f);
  };

  for (float s : interleavedLR) {
    int16_t v = clip(s);
    f.put(char(v & 0xFF));
    f.put(char((v >> 8) & 0xFF));
  }

  return true;
}

} // namespace mforce
