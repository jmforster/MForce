// STK Flute reference renderer — ground truth for the MForce port.
// Same conventions as bowed_ref.cpp / clarinet_ref.cpp.

#include "Flute.h"
#include "Stk.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

using namespace stk;

static double kSampleRate = 48000.0;  // argv[2] overrides
static const double kNoteSec = 2.0;
static const double kOffAt = 1.7;
static const int kNotes[5] = {48, 60, 72, 84, 96};

static double midi_hz(int m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

static bool write_wav16_mono(const std::string& path,
                             const std::vector<double>& samples) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t n = (uint32_t)samples.size();
    uint32_t dataBytes = n * 2, sr = (uint32_t)kSampleRate;
    uint32_t byteRate = sr * 2, riffSize = 36 + dataBytes, fmtSize = 16;
    uint16_t fmt = 1, ch = 1, block = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    fwrite(&fmtSize, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
    fwrite(&sr, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&block, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
    for (double s : samples) {
        if (s > 1.0) s = 1.0;
        if (s < -1.0) s = -1.0;
        int16_t v = (int16_t)std::lrint(s * 32767.0);
        fwrite(&v, 2, 1, f);
    }
    fclose(f);
    return true;
}

struct Variant {
    const char* name;
    double amplitude;
    double jet;      // CC2 0..128 -> jetRatio 0.08 + 0.48*(v/128); <0 = default 0.32
    double noise;    // CC4 0..128 -> gain 0.4*(v/128); <0 = default 0.15
    double vibGain;  // CC1 0..128 -> gain 0.4*(v/128); <0 = default 0.05
};

static const Variant kVariants[] = {
    {"default",   0.8, -1.0, -1.0, -1.0},
    {"jet_lo",    0.8, 32.0, -1.0, -1.0},   // jetRatio 0.20
    {"jet_hi",    0.8, 96.0, -1.0, -1.0},   // jetRatio 0.44
    {"noise_hi",  0.8, -1.0, 96.0, -1.0},   // noiseGain 0.30
    {"noise_off", 0.8, -1.0, 0.0, -1.0},
    {"vib_off",   0.8, -1.0, -1.0, 0.0},
    {"soft",      0.3, -1.0, -1.0, -1.0},
    {"hard",      1.0, -1.0, -1.0, -1.0},
};

int main(int argc, char** argv) {
    std::string outdir = argc > 1 ? argv[1] : ".";
    if (argc > 2) kSampleRate = atof(argv[2]);
    Stk::setSampleRate(kSampleRate);

    const int noteFrames = (int)(kNoteSec * kSampleRate);
    const int offFrame = (int)(kOffAt * kSampleRate);

    for (const Variant& v : kVariants) {
        Flute flute(8.0);
        std::vector<double> out;
        out.reserve(noteFrames * 5);

        for (int mi = 0; mi < 5; ++mi) {
            flute.clear();
            if (v.jet >= 0.0) flute.controlChange(2, v.jet);
            if (v.noise >= 0.0) flute.controlChange(4, v.noise);
            if (v.vibGain >= 0.0) flute.controlChange(1, v.vibGain);
            flute.noteOn(midi_hz(kNotes[mi]), v.amplitude);
            for (int i = 0; i < noteFrames; ++i) {
                if (i == offFrame) flute.noteOff(0.5);
                out.push_back(flute.tick());
            }
        }

        std::string path = outdir + "/stk_flute_" + v.name + ".wav";
        if (!write_wav16_mono(path, out)) {
            std::fprintf(stderr, "FAILED to write %s\n", path.c_str());
            return 1;
        }
        std::printf("wrote %s\n", path.c_str());
    }
    return 0;
}
