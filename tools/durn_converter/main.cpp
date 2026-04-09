// durn_converter — Converts raw music files (ABC, kern) to DURN format
//
// Usage:
//   durn_converter <raw_dir> --genre <genre> --out <durn_dir> [--all]
//
// Defaults to --new (skip files that already have a .txt in output dir)
// Use --all to reconvert everything

#include "../../research/abc/abc_parser.h"
#include "../../research/kern/kern_parser.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <string>
#include <algorithm>
#include <cmath>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Convert ABC FigureUnitSequence to DURN text
// ---------------------------------------------------------------------------
static std::string duration_prefix(double beats) {
    // Find best match: w=4, h.=3, h=2, q.=1.5, q=1, e.=0.75, e=0.5, s.=0.375, s=0.25
    struct DurMap { const char* tok; double val; };
    static const DurMap map[] = {
        {"w.", 6.0}, {"w", 4.0}, {"h.", 3.0}, {"h", 2.0},
        {"q.", 1.5}, {"q", 1.0}, {"e.", 0.75}, {"e", 0.5},
        {"s.", 0.375}, {"s", 0.25}
    };
    double best_diff = 1e9;
    const char* best = "q";
    for (auto& d : map) {
        double diff = std::abs(d.val - beats);
        if (diff < best_diff) {
            best_diff = diff;
            best = d.tok;
        }
    }
    return best;
}

static std::string step_suffix(int step, bool is_rest) {
    if (is_rest) return "r";
    if (step == 0) return "n";
    if (step > 0) return "u" + std::to_string(step);
    return "d" + std::to_string(-step);
}

// ---------------------------------------------------------------------------
// Generic unit → DURN token emitter
// ---------------------------------------------------------------------------
struct GenericUnit {
    float duration;
    int step;
    bool is_rest;
};

static std::string emit_durn(const std::string& header,
                             const std::vector<GenericUnit>& units,
                             double beats_per_bar,
                             double pickupBeats = 0) {
    std::ostringstream out;
    out << header << "\n";

    // Emit pickup notes first, then insert | before the first full bar.
    // beat_in_bar tracks position within the current bar.
    double beat_in_bar = 0;
    double pickup_remaining = pickupBeats;
    int bar_count = 0;

    for (size_t i = 0; i < units.size(); ++i) {
        auto& u = units[i];
        std::string tok = duration_prefix(u.duration) +
                          step_suffix(u.step, u.is_rest);

        out << tok;

        // Track pickup separately: once pickup is consumed, insert first barline
        if (pickup_remaining > 0.01) {
            pickup_remaining -= u.duration;
            if (pickup_remaining <= 0.01 && i + 1 < units.size()) {
                out << " | ";
                beat_in_bar = 0;
            } else if (i + 1 < units.size()) {
                out << " ";
            }
            continue;
        }

        beat_in_bar += u.duration;

        if (beat_in_bar >= beats_per_bar - 0.01) {
            bar_count++;
            beat_in_bar = std::fmod(beat_in_bar, beats_per_bar);

            bool phrase_break = (bar_count % 4 == 0) && (i + 1 < units.size());
            bool figure_break = !phrase_break && (i + 1 < units.size());

            if (phrase_break) {
                out << " /\n";
            } else if (figure_break) {
                out << " | ";
            }
        } else if (i + 1 < units.size()) {
            out << " ";
        }
    }
    out << "\n";
    return out.str();
}

// ---------------------------------------------------------------------------
// Parse meter string to beats per bar
// ---------------------------------------------------------------------------
static double parse_beats_per_bar(const std::string& meter) {
    auto slash = meter.find('/');
    if (slash != std::string::npos) {
        int num = std::stoi(meter.substr(0, slash));
        int den = std::stoi(meter.substr(slash + 1));
        return double(num) * (4.0 / double(den));
    }
    return 4.0;
}

// ---------------------------------------------------------------------------
// ABC → DURN
// ---------------------------------------------------------------------------
static std::string abc_to_durn(const abc::AbcTune& tune, const std::string& rawText) {
    auto seq = abc::to_figure_units(tune);

    // Build header
    std::string keyName = tune.key.tonic;
    std::string scaleName;
    if (tune.key.mode == "major" || tune.key.mode == "")
        scaleName = "Major";
    else if (tune.key.mode == "minor")
        scaleName = "Minor";
    else if (tune.key.mode == "dorian")
        scaleName = "Dorian";
    else if (tune.key.mode == "mixolydian")
        scaleName = "Mixolydian";
    else if (tune.key.mode == "phrygian")
        scaleName = "Phrygian";
    else if (tune.key.mode == "lydian")
        scaleName = "Lydian";
    else if (tune.key.mode == "locrian")
        scaleName = "Locrian";
    else
        scaleName = "Major";

    // Detect starting degree from first non-rest note
    int startDeg = 0;
    for (auto& n : tune.notes) {
        if (!n.is_rest) {
            int semi = abc::note_name_to_semitone(n.note_name[0]);
            int acc = n.accidental;
            if (acc == abc::AbcNote::ACC_DEFAULT)
                acc = tune.key.get_accidental(n.note_name[0]);
            semi += acc;
            int rel = ((semi - tune.key.tonic_semitone()) % 12 + 12) % 12;
            startDeg = tune.key.semitone_to_scale_degree(rel);
            break;
        }
    }

    double bpb = parse_beats_per_bar(tune.meter_str);

    // Detect pickup: count note-letter characters before the first '|'
    // in the body of the raw ABC text (after the K: header line).
    double pickupBeats = 0;
    {
        // Find body start (after K: line)
        size_t bodyStart = 0;
        size_t kPos = rawText.find("\nK:");
        if (kPos != std::string::npos) {
            bodyStart = rawText.find('\n', kPos + 1);
            if (bodyStart != std::string::npos) bodyStart++;
            else bodyStart = rawText.size();
        }

        int notesBeforeBar = 0;
        for (size_t p = bodyStart; p < rawText.size(); ++p) {
            char ch = rawText[p];
            if (ch == '|') break;
            if ((ch >= 'a' && ch <= 'g') || (ch >= 'A' && ch <= 'G') ||
                ch == 'z' || ch == 'Z')
                notesBeforeBar++;
        }

        double sum = 0;
        for (int i = 0; i < notesBeforeBar && i < int(tune.notes.size()); ++i)
            sum += tune.notes[i].duration_beats;
        if (sum > 0.01 && sum < bpb - 0.01)
            pickupBeats = sum;
    }

    std::string header = "key:" + keyName + " scale:" + scaleName +
                         " meter:" + tune.meter_str + " bpm:120" +
                         " start:" + std::to_string(startDeg);
    if (pickupBeats > 0.01)
        header += " pickup:" + std::to_string(pickupBeats).substr(0, 4);

    // Convert to generic units
    std::vector<GenericUnit> units;
    for (size_t i = 0; i < seq.units.size(); ++i) {
        bool rest = (i < tune.notes.size() && tune.notes[i].is_rest);
        units.push_back({float(seq.units[i].duration), seq.units[i].step, rest});
    }

    return emit_durn(header, units, bpb, pickupBeats);
}

// ---------------------------------------------------------------------------
// Kern → DURN
// ---------------------------------------------------------------------------
static std::string kern_to_durn(const std::string& path) {
    auto cr = kern::convert_file(path, kern::VoiceSelect::Last,
                                 kern::RestHandling::AsZeroStep);

    // Build header from metadata
    std::string keyName = "C";
    std::string scaleName = "Major";
    if (!cr.metadata.key.empty()) {
        std::string k = cr.metadata.key;
        if (!k.empty() && k.back() == ':') k.pop_back();
        if (!k.empty() && std::islower(k[0])) {
            scaleName = "Minor";
            k[0] = std::toupper(k[0]);
        }
        keyName = k;
    }

    std::string meter = cr.metadata.meter.empty() ? "4/4" : cr.metadata.meter;

    // Detect starting degree from first non-rest note
    auto kf = kern::parse_file(path, kern::VoiceSelect::Last);
    auto merged = kern::merge_ties(kf.melody);

    kern::ScaleMap scaleMap = kern::detect_scale(kf.metadata);
    int startDeg = 0;
    for (auto& n : merged) {
        if (!n.is_rest) {
            auto dr = scaleMap.pitch_to_degree(n.midi_pitch);
            startDeg = dr.degree;
            break;
        }
    }

    std::string header = "key:" + keyName + " scale:" + scaleName +
                         " meter:" + meter + " bpm:120" +
                         " start:" + std::to_string(startDeg);

    std::vector<GenericUnit> units;
    for (size_t i = 0; i < cr.units.size() && i < merged.size(); ++i) {
        units.push_back({cr.units[i].duration, cr.units[i].step, merged[i].is_rest});
    }

    return emit_durn(header, units, parse_beats_per_bar(meter));
}

// ---------------------------------------------------------------------------
// Stem: filename without extension
// ---------------------------------------------------------------------------
static std::string stem(const fs::path& p) {
    return p.stem().string();
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    if (argc < 5) {
        std::cerr << "Usage: durn_converter <raw_dir> --genre <genre> --out <durn_dir> [--all]\n"
                  << "  Converts ABC/kern files to DURN format.\n"
                  << "  --new (default): skip files already converted\n"
                  << "  --all: reconvert everything\n";
        return 1;
    }

    std::string rawDir;
    std::string genre;
    std::string outDir;
    bool convertAll = false;

    rawDir = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--genre" && i + 1 < argc) genre = argv[++i];
        else if (arg == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (arg == "--all") convertAll = true;
        else if (arg == "--new") convertAll = false;
    }

    if (genre.empty() || outDir.empty()) {
        std::cerr << "Error: --genre and --out are required\n";
        return 1;
    }

    fs::path outPath = fs::path(outDir) / genre;
    fs::create_directories(outPath);

    if (!fs::exists(rawDir)) {
        std::cerr << "Error: raw directory not found: " << rawDir << "\n";
        return 1;
    }

    int converted = 0, skipped = 0, failed = 0;

    for (auto& entry : fs::directory_iterator(rawDir)) {
        if (!entry.is_regular_file()) continue;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

        // Handle ABC and kern
        if (ext != ".abc" && ext != ".krn") continue;

        std::string name = stem(entry.path());
        fs::path outFile = outPath / (name + ".txt");

        // Skip if already exists (unless --all)
        if (!convertAll && fs::exists(outFile)) {
            skipped++;
            continue;
        }

        try {
            // Read ABC file
            std::ifstream f(entry.path());
            std::string content((std::istreambuf_iterator<char>(f)),
                                 std::istreambuf_iterator<char>());

            std::string durn;
            if (ext == ".abc") {
                abc::AbcParser parser;
                auto tune = parser.parse(content);
                if (tune.notes.empty()) {
                    std::cerr << "  SKIP (no notes): " << name << "\n";
                    skipped++;
                    continue;
                }
                durn = abc_to_durn(tune, content);
            } else {
                // .krn
                durn = kern_to_durn(entry.path().string());
            }

            std::ofstream of(outFile);
            of << durn;

            std::cout << "  OK: " << name << " → " << outFile.string() << "\n";
            converted++;
        } catch (const std::exception& e) {
            std::cerr << "  FAIL: " << name << " — " << e.what() << "\n";
            failed++;
        }
    }

    std::cout << "\nDone: " << converted << " converted, "
              << skipped << " skipped, " << failed << " failed\n";
    return 0;
}
