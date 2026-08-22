#include "mforce/render/wav_reader.h"
#include <cstdint>
#include <fstream>
#include <cstring>

namespace mforce {

static bool read_u32(std::ifstream& f, uint32_t& v) {
    uint8_t b[4];
    f.read(reinterpret_cast<char*>(b), 4);
    if (f.gcount() != 4) return false;
    v = uint32_t(b[0]) | (uint32_t(b[1]) << 8)
      | (uint32_t(b[2]) << 16) | (uint32_t(b[3]) << 24);
    return true;
}

static bool read_u16(std::ifstream& f, uint16_t& v) {
    uint8_t b[2];
    f.read(reinterpret_cast<char*>(b), 2);
    if (f.gcount() != 2) return false;
    v = uint16_t(b[0]) | (uint16_t(b[1]) << 8);
    return true;
}

static bool read_tag(std::ifstream& f, char tag[5]) {
    f.read(tag, 4);
    tag[4] = '\0';
    return f.gcount() == 4;
}

bool read_wav_16le_stereo(const std::string& path,
                          std::vector<float>& out,
                          int& outSampleRate) {
    out.clear();
    outSampleRate = 0;

    std::ifstream f(path, std::ios::binary);
    if (!f) return false;

    char tag[5];
    uint32_t chunkSize = 0;

    // "RIFF"<size>"WAVE"
    if (!read_tag(f, tag) || std::strcmp(tag, "RIFF") != 0) return false;
    if (!read_u32(f, chunkSize))                            return false;
    if (!read_tag(f, tag) || std::strcmp(tag, "WAVE") != 0) return false;

    bool gotFmt   = false;
    bool gotData  = false;
    uint16_t numChannels  = 0;
    uint16_t bitsPerSample = 0;
    uint16_t formatTag    = 0;
    uint32_t sampleRate   = 0;
    uint32_t dataBytes    = 0;
    std::streampos dataStart = 0;

    while (f && !(gotFmt && gotData)) {
        if (!read_tag(f, tag))         return false;
        if (!read_u32(f, chunkSize))   return false;

        if (std::strcmp(tag, "fmt ") == 0) {
            if (chunkSize < 16) return false;
            uint32_t avgByteRate = 0;
            uint16_t blockAlign  = 0;
            if (!read_u16(f, formatTag))    return false;
            if (!read_u16(f, numChannels))  return false;
            if (!read_u32(f, sampleRate))   return false;
            if (!read_u32(f, avgByteRate))  return false;
            if (!read_u16(f, blockAlign))   return false;
            if (!read_u16(f, bitsPerSample))return false;
            // Skip any extra fmt bytes (e.g. WAVE_FORMAT_EXTENSIBLE extension)
            if (chunkSize > 16) f.seekg(chunkSize - 16, std::ios::cur);
            gotFmt = true;
        } else if (std::strcmp(tag, "data") == 0) {
            dataBytes = chunkSize;
            dataStart = f.tellg();
            gotData = true;
            // Don't read samples yet — wait until we know fmt for sure.
        } else {
            // Unknown chunk — skip
            f.seekg(chunkSize, std::ios::cur);
        }
    }

    if (!gotFmt || !gotData)          return false;
    if (formatTag != 1)               return false;  // PCM only
    if (numChannels != 2)             return false;  // stereo only
    if (bitsPerSample != 16)          return false;  // 16-bit only

    outSampleRate = int(sampleRate);

    f.clear();
    f.seekg(dataStart);
    int numSamples = int(dataBytes / 2);  // 2 bytes/sample, L+R interleaved
    out.resize(size_t(numSamples));
    for (int i = 0; i < numSamples; ++i) {
        uint8_t b[2];
        f.read(reinterpret_cast<char*>(b), 2);
        if (f.gcount() != 2) {
            out.resize(size_t(i));
            return i > 0;
        }
        int16_t v = int16_t(uint16_t(b[0]) | (uint16_t(b[1]) << 8));
        out[size_t(i)] = float(v) / 32768.0f;
    }
    return true;
}

} // namespace mforce
