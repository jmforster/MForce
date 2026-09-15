// STK BlowHole reference renderer — ground truth for the MForce port.
// Clarinet + two-port register vent + three-port dynamic tonehole
// (Scavone & Cook 1998). Same conventions as clarinet_ref.cpp; argv[2]
// = sample rate (22050 for native-rate validation).
// NOTE: reed/vent and tonehole/bell distances are FIXED (5 and 4
// samples at 22050 scaled by rate), so vent/tonehole influence varies
// with playing frequency — that is the model, not a bug.

#include "BlowHole.h"
#include "Stk.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

using namespace stk;

static double kSampleRate = 48000.0;
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
    double tonehole;   // CC11 0..128 -> openness v/128; <0 = default (open)
    double vent;       // CC1  0..128 -> openness v/128; <0 = default (closed)
    double stiffness;  // CC2  0..128 -> slope -0.44 + 0.26*(v/128); <0 = default -0.3
    double noise;      // CC4  0..128 -> gain 0.4*(v/128); <0 = default 0.2
};

static const Variant kVariants[] = {
    {"default",   0.8, -1.0, -1.0, -1.0, -1.0},   // tonehole open, vent closed
    {"th_closed", 0.8, 0.0, -1.0, -1.0, -1.0},
    {"th_half",   0.8, 64.0, -1.0, -1.0, -1.0},
    {"vent_open", 0.8, -1.0, 128.0, -1.0, -1.0},
    {"both",      0.8, 0.0, 128.0, -1.0, -1.0},   // th closed + vent open
    {"reed_soft", 0.8, -1.0, -1.0, 19.2, -1.0},
    {"reed_hard", 0.8, -1.0, -1.0, 115.2, -1.0},
    {"noise_hi",  0.8, -1.0, -1.0, -1.0, 96.0},
    {"noise_off", 0.8, -1.0, -1.0, -1.0, 0.0},
};

int main(int argc, char** argv) {
    std::string outdir = argc > 1 ? argv[1] : ".";
    if (argc > 2) kSampleRate = atof(argv[2]);
    Stk::setSampleRate(kSampleRate);

    const int noteFrames = (int)(kNoteSec * kSampleRate);
    const int offFrame = (int)(kOffAt * kSampleRate);

    for (const Variant& v : kVariants) {
        BlowHole bh(8.0);
        std::vector<double> out;
        out.reserve(noteFrames * 5);

        for (int mi = 0; mi < 5; ++mi) {
            bh.clear();
            if (v.tonehole >= 0.0) bh.controlChange(11, v.tonehole);
            if (v.vent >= 0.0) bh.controlChange(1, v.vent);
            if (v.stiffness >= 0.0) bh.controlChange(2, v.stiffness);
            if (v.noise >= 0.0) bh.controlChange(4, v.noise);
            bh.noteOn(midi_hz(kNotes[mi]), v.amplitude);
            for (int i = 0; i < noteFrames; ++i) {
                if (i == offFrame) bh.noteOff(0.5);
                out.push_back(bh.tick());
            }
        }

        std::string path = outdir + "/stk_blowhole_" + v.name + ".wav";
        if (!write_wav16_mono(path, out)) {
            std::fprintf(stderr, "FAILED to write %s\n", path.c_str());
            return 1;
        }
        std::printf("wrote %s\n", path.c_str());
    }
    return 0;
}
