// STK BandedWG reference renderer — ground truth for the MForce port.
// Banded waveguides (Essl & Cook): N bandpass+delay mode loops, struck
// (pluck into the delay lines) or bowed (BowTable across the mode sum).
// Presets: 0 uniform bar, 1 tuned bar, 2 glass harmonica, 3 Tibetan
// prayer bowl. Same 5-note/2s conventions; argv[2] = sample rate
// (22050 for native-rate validation). Percussive family — notes ring
// through the slot on the struck cells (noteOff is a no-op for plucks).

#include "BandedWG.h"
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
    int preset;        // 0 bar, 1 tuned bar, 2 glass, 3 bowl
    bool bowed;        // false = struck (pluck), true = bowed (CC64=0 bows)
    double bowPress;   // CC2 0..128 -> table slope 10-9*(v/128); <0 default 3.0
};

static const Variant kVariants[] = {
    {"bar_struck",    0, false, -1.0},
    {"bar_bowed",     0, true,  -1.0},
    {"tbar_struck",   1, false, -1.0},
    {"tbar_bowed",    1, true,  -1.0},
    {"glass_struck",  2, false, -1.0},
    {"glass_bowed",   2, true,  -1.0},
    {"bowl_struck",   3, false, -1.0},
    {"bowl_bowed",    3, true,  -1.0},
    {"bowl_bowed_hard", 3, true, 115.2},
};

int main(int argc, char** argv) {
    std::string outdir = argc > 1 ? argv[1] : ".";
    if (argc > 2) kSampleRate = atof(argv[2]);
    Stk::setSampleRate(kSampleRate);

    const int noteFrames = (int)(kNoteSec * kSampleRate);
    const int offFrame = (int)(kOffAt * kSampleRate);

    for (const Variant& v : kVariants) {
        BandedWG wg;
        std::vector<double> out;
        // Bowed banded loops bloom over ~4 s (measured: energy doubles
        // every ~0.5 s, saturates ~4 s at 22050) — bowed variants get
        // 5 s slots so the reference actually demonstrates the sound.
        const double slotSec = v.bowed ? 5.0 : kNoteSec;
        const double offSec = v.bowed ? 4.5 : kOffAt;
        const int slotFrames = (int)(slotSec * kSampleRate);
        const int slotOff = (int)(offSec * kSampleRate);
        out.reserve(slotFrames * 5);

        for (int mi = 0; mi < 5; ++mi) {
            wg.clear();
            wg.setPreset(v.preset);
            // CC64 (__SK_Sustain_): value < 65 -> struck, >= 65 -> bowed
            wg.controlChange(64, v.bowed ? 127.0 : 0.0);
            if (v.bowPress >= 0.0) wg.controlChange(2, v.bowPress);
            wg.noteOn(midi_hz(kNotes[mi]), 0.8);
            for (int i = 0; i < slotFrames; ++i) {
                if (i == slotOff) wg.noteOff(0.5);
                out.push_back(wg.tick());
            }
        }

        std::string path = outdir + "/stk_bandedwg_" + v.name + ".wav";
        if (!write_wav16_mono(path, out)) {
            std::fprintf(stderr, "FAILED to write %s\n", path.c_str());
            return 1;
        }
        std::printf("wrote %s\n", path.c_str());
    }
    return 0;
}
