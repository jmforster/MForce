#include "mforce/render/patch_loader.h"
#include "mforce/render/wav_writer.h"
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include "mforce/music/conductor.h"
#include <iostream>
#include <vector>
#include <filesystem>
#include <cmath>
#include <algorithm>
#include <string>
#include <sstream>

using namespace mforce;

// ---------------------------------------------------------------------------
// Parse a chord token like "C:M", "Am:m7", "G:7", "F:M"
// Format: root:chordType  (root = pitch name, chordType = ChordDef short name)
// ---------------------------------------------------------------------------
static Chord parse_chord_token(const std::string& token, int octave,
                               const std::string& dictName, float durationBeats) {
    auto colon = token.find(':');
    if (colon == std::string::npos)
        throw std::runtime_error("Bad chord token (expected Root:Type): " + token);

    std::string rootName = token.substr(0, colon);
    std::string chordType = token.substr(colon + 1);

    if (dictName.empty())
        return Chord::create(rootName, octave, chordType, durationBeats);
    else
        return Chord::create(dictName, rootName, octave, chordType, durationBeats);
}

// ---------------------------------------------------------------------------
// Chord progression mode
// ---------------------------------------------------------------------------
static int run_chords(int argc, char** argv) {
    // mforce_cli --chords <patch.json> <out.wav> <bpm> <beats-per-chord> <chord1> <chord2> ...
    // Optional: --dict <name> --octave <n>
    if (argc < 7) {
        std::cerr << "Usage: mforce_cli --chords <patch.json> <out.wav> <bpm> <beats-per-chord> <chord>...\n"
                  << "  Options: --dict <name> --octave <n>\n"
                  << "  Example: mforce_cli --chords patches/guitar_pluck.json renders/chords.wav 120 4 C:M F:M G:7 C:M\n";
        return 1;
    }

    std::string patchPath = argv[2];
    std::string outPath   = argv[3];
    float bpm             = std::stof(argv[4]);
    float beatsPerChord   = std::stof(argv[5]);

    // Parse optional flags and collect chord tokens
    std::string dictName;
    int octave = 3;
    float volume = 1.0f;
    float hiBoost = 0.0f;
    std::vector<std::string> chordTokens;

    for (int i = 6; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--dict" && i + 1 < argc) {
            dictName = argv[++i];
        } else if (arg == "--octave" && i + 1 < argc) {
            octave = std::stoi(argv[++i]);
        } else if (arg == "--volume" && i + 1 < argc) {
            volume = std::stof(argv[++i]);
        } else if (arg == "--hiboost" && i + 1 < argc) {
            hiBoost = std::stof(argv[++i]);
        } else {
            chordTokens.push_back(arg);
        }
    }

    if (chordTokens.empty()) {
        std::cerr << "No chords specified.\n";
        return 1;
    }

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch file not found: " << patchPath << "\n";
        return 1;
    }

    // Load instrument
    auto ip = load_instrument_patch(patchPath);
    ip.instrument->volume = volume;
    ip.instrument->hiBoost = hiBoost;

    // Build Part from chord tokens
    Part part;
    part.name = "chords";
    for (const auto& token : chordTokens) {
        Chord chord = parse_chord_token(token, octave, dictName, beatsPerChord);
        part.add_chord(chord);
    }

    // Perform
    Conductor conductor;
    conductor.chordPerformer.defaultSpreadMs = 15.0f;
    conductor.perform(part, bpm, *ip.instrument);

    // Render
    float totalSeconds = part.totalBeats * 60.0f / bpm + 1.0f; // +1s tail
    int frames = int(totalSeconds * float(ip.sampleRate));
    std::vector<float> mono(frames, 0.0f);
    ip.instrument->render(mono.data(), frames);

    // Convert mono to stereo
    std::vector<float> stereo(frames * 2);
    for (int i = 0; i < frames; ++i) {
        stereo[i * 2]     = mono[i];
        stereo[i * 2 + 1] = mono[i];
    }

    if (!write_wav_16le_stereo(outPath, ip.sampleRate, stereo)) {
        std::cerr << "Failed to write wav: " << outPath << "\n";
        return 1;
    }

    // Stats
    float peak = 0.0f;
    double rms = 0.0;
    int nonzero = 0;
    for (auto s : stereo) {
        if (s != 0.0f) nonzero++;
        float a = std::fabs(s);
        if (a > peak) peak = a;
        rms += double(s) * double(s);
    }
    rms = std::sqrt(rms / stereo.size());

    std::cout << "Wrote: " << outPath
              << " (" << frames << " frames @ " << ip.sampleRate << " Hz)\n";
    std::cout << "  " << chordTokens.size() << " chords, "
              << part.totalBeats << " beats @ " << bpm << " bpm\n";
    std::cerr << "  peak=" << peak
              << " rms=" << rms
              << " nonzero=" << nonzero << "/" << stereo.size() << "\n";

    return 0;
}

// ---------------------------------------------------------------------------
// Standard patch render mode
// ---------------------------------------------------------------------------
static int run_patch(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: mforce_cli <patch.json> <out.wav>\n";
        return 1;
    }

    std::string patchPath = argv[1];
    std::string outPath   = argv[2];

    if (!std::filesystem::exists(patchPath)) {
        std::cerr << "Patch file not found: " << patchPath << "\n";
        return 1;
    }

    Patch p = load_patch_file(patchPath);

    std::vector<float> out((size_t)p.frames * 2);
    p.mixer->render(out.data(), p.frames);

    if (!write_wav_16le_stereo(outPath, p.sampleRate, out)) {
        std::cerr << "Failed to write wav: " << outPath << "\n";
        return 1;
    }

    float peak = 0.0f;
    double rms = 0.0;
    int nonzero = 0;
    for (auto s : out) {
        if (s != 0.0f) nonzero++;
        float a = std::fabs(s);
        if (a > peak) peak = a;
        rms += double(s) * double(s);
    }
    rms = std::sqrt(rms / out.size());

    std::cout << "Wrote: " << outPath
              << " (" << p.frames << " frames @ "
              << p.sampleRate << " Hz)\n";
    std::cerr << "  peak=" << peak
              << " rms=" << rms
              << " nonzero=" << nonzero << "/" << out.size() << "\n";

    return 0;
}

int main(int argc, char** argv)
{
    try {
        if (argc >= 2 && std::string(argv[1]) == "--chords")
            return run_chords(argc, argv);

        return run_patch(argc, argv);
    }
    catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
