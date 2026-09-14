// STK Bowed reference renderer — ground truth for the MForce port.
// Steal-first campaign (steering meeting 2026-09-13): render the proven
// model's output deterministically so the port can be verified against
// measured criteria instead of ears.
//
// Output format matches the campaign render convention: 5 notes C3..C7,
// 2 s each, mono 16-bit 48 kHz, one WAV per feature variant.
//
// Build: cmake -S tools/stk_ref -B tools/stk_ref/build && cmake --build tools/stk_ref/build --config Release
// Run:   tools/stk_ref/build/Release/stk_bowed_ref.exe <outdir>

#include "Bowed.h"
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
static const double kOffAt = 1.7;      // bow lift inside each 2 s slot
static const int kNotes[5] = {48, 60, 72, 84, 96}; // C3..C7 MIDI

static double midi_hz(int m) { return 440.0 * std::pow(2.0, (m - 69) / 12.0); }

static bool write_wav16_mono(const std::string& path,
                             const std::vector<double>& samples) {
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) return false;
    uint32_t n = (uint32_t)samples.size();
    uint32_t dataBytes = n * 2, sr = (uint32_t)kSampleRate;
    uint32_t byteRate = sr * 2;
    uint32_t riffSize = 36 + dataBytes;
    uint16_t fmt = 1, ch = 1, block = 2, bits = 16;
    fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f);
    uint32_t fmtSize = 16;
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
    double amplitude;   // noteOn amplitude (bow velocity + attack rate)
    double pressure;    // CC2 0..128 -> bowTable slope 5 - 4*(v/128); <0 = leave default (3.0)
    double position;    // CC4 0..128 -> betaRatio v/128; <0 = leave default (0.127236)
    double vibGain;     // CC1 0..128 -> gain 0.4*(v/128)
    double vibFreq;     // CC11 0..128 -> freq 12*(v/128); <0 = leave default (6.12723)
};

// Full feature tour of the model's control space.
static const Variant kVariants[] = {
    {"default",        0.8, -1.0, -1.0,  0.0, -1.0},
    {"press_lo",       0.8, 20.0, -1.0,  0.0, -1.0},  // slope 4.375 (light pulse)
    {"press_hi",       0.8, 115.0, -1.0, 0.0, -1.0},  // slope 1.406 (wide pulse)
    {"pos_bridge",     0.8, -1.0,  8.0,  0.0, -1.0},  // beta 0.0625 (sul ponticello)
    {"pos_middle",     0.8, -1.0, 32.0,  0.0, -1.0},  // beta 0.25 (sul tasto side)
    {"vibrato",        0.8, -1.0, -1.0, 90.0, 60.0},  // gain 0.281, 5.625 Hz
    {"soft",           0.3, -1.0, -1.0,  0.0, -1.0},  // maxVelocity 0.09, slow attack
    {"hard",           1.0, -1.0, -1.0,  0.0, -1.0},  // maxVelocity 0.23, fast attack
    {"press_lo_soft",  0.3, 20.0, -1.0,  0.0, -1.0},  // combo corner
    {"press_hi_hard",  1.0, 115.0, -1.0, 0.0, -1.0},  // combo corner
    // SKINI-style slur: one noteOn, then CC101 frequency steps with the
    // bow held — the string state persists, notes 2..5 pay no ignition.
    {"legato",         0.8, -1.0, -1.0,  0.0, -1.0},
};

int main(int argc, char** argv) {
    std::string outdir = argc > 1 ? argv[1] : ".";
    if (argc > 2) kSampleRate = atof(argv[2]);
    Stk::setSampleRate(kSampleRate);

    const int noteFrames = (int)(kNoteSec * kSampleRate);
    const int offFrame = (int)(kOffAt * kSampleRate);

    for (const Variant& v : kVariants) {
        // Fresh instrument per variant; constructed after setSampleRate so
        // the string filter pole and delay sizing see 48 kHz.
        Bowed bowed(8.0);
        std::vector<double> out;
        out.reserve(noteFrames * 5);

        const bool legato = std::string(v.name) == "legato";
        for (int mi = 0; mi < 5; ++mi) {
            if (legato && mi > 0) {
                // Slur: pitch change on the sounding string, bow held.
                bowed.controlChange(101, midi_hz(kNotes[mi]));
                for (int i = 0; i < noteFrames; ++i) {
                    if (mi == 4 && i == offFrame) bowed.noteOff(0.5);
                    out.push_back(bowed.tick());
                }
                continue;
            }
            bowed.clear();
            // Controls re-applied per note: setFrequency (inside noteOn)
            // re-derives both delay lengths from betaRatio, so position
            // must be set before noteOn of every note.
            if (v.pressure >= 0.0) bowed.controlChange(2, v.pressure);
            if (v.position >= 0.0) bowed.controlChange(4, v.position);
            bowed.controlChange(1, v.vibGain);
            if (v.vibFreq >= 0.0) bowed.controlChange(11, v.vibFreq);

            bowed.noteOn(midi_hz(kNotes[mi]), v.amplitude);
            for (int i = 0; i < noteFrames; ++i) {
                if ((!legato || mi == 4) && i == offFrame) bowed.noteOff(0.5);
                out.push_back(bowed.tick());
            }
        }

        std::string path = outdir + "/stk_bowed_" + v.name + ".wav";
        if (!write_wav16_mono(path, out)) {
            std::fprintf(stderr, "FAILED to write %s\n", path.c_str());
            return 1;
        }
        std::printf("wrote %s\n", path.c_str());
    }
    return 0;
}
