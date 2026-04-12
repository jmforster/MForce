// durn_converter — Converts raw music files (ABC, kern) to DURN format
//
// Usage:
//   durn_converter <raw_dir> --genre <genre> --out <durn_dir> [--all]
//
// Defaults to --new (skip files that already have a .txt in output dir)
// Use --all to reconvert everything

#include "parsers/abc_parser.h"
#include "parsers/kern_parser.h"
#include "parsers/midi_parser.h"
#include "parsers/musicxml_parser.h"
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

static std::string step_suffix(int step, bool is_rest, int accidental = 0) {
    if (is_rest) return "r";
    std::string suffix;
    if (step == 0) suffix = "n";
    else if (step > 0) suffix = "u" + std::to_string(step);
    else suffix = "d" + std::to_string(-step);
    if (accidental > 0) suffix += "+";
    else if (accidental < 0) suffix += "-";
    return suffix;
}

// ---------------------------------------------------------------------------
// Generic unit → DURN token emitter
// ---------------------------------------------------------------------------
struct GenericUnit {
    float duration;
    int step;
    bool is_rest;
    int accidental{0};  // +1=sharp, -1=flat
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
                          step_suffix(u.step, u.is_rest, u.accidental);

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

    // Build header from metadata — normalize kern key names
    // Kern uses e.g. "A-:" for Ab major, "f#:" for F# minor
    std::string keyName = "C";
    std::string scaleName = "Major";
    if (!cr.metadata.key.empty()) {
        std::string k = cr.metadata.key;
        if (!k.empty() && k.back() == ':') k.pop_back();
        if (!k.empty() && std::islower(k[0])) {
            scaleName = "Minor";
            k[0] = std::toupper(k[0]);
        }
        // Convert kern accidentals: - → b, # stays
        std::string normalized;
        for (char c : k) {
            if (c == '-') normalized += 'b';
            else normalized += c;
        }
        keyName = normalized;
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

    // Convert to generic units with accidentals for chromatic notes
    std::vector<GenericUnit> units;
    int prevMidi = -1;
    bool first = true;
    for (size_t i = 0; i < merged.size(); ++i) {
        auto& n = merged[i];
        if (n.is_rest) {
            units.push_back({n.duration_beats, 0, true, 0});
            continue;
        }

        // Compute diatonic step
        int curDeg = scaleMap.absolute_degree(n.midi_pitch);
        int step = first ? 0 : (curDeg - scaleMap.absolute_degree(prevMidi));

        // Check for accidental (chromatic remainder)
        auto dr = scaleMap.pitch_to_degree(n.midi_pitch);
        int acc = dr.chromatic_remainder;  // 0 = diatonic, +1 = sharp, -1 = flat

        units.push_back({n.duration_beats, step, false, acc});
        first = false;
        prevMidi = n.midi_pitch;
    }

    return emit_durn(header, units, parse_beats_per_bar(meter));
}

// ---------------------------------------------------------------------------
// MIDI → DURN
// ---------------------------------------------------------------------------
static std::string midi_to_durn(const std::string& path, int trackIdx = -1,
                                int keyRoot = -1, bool keyMinor = false,
                                bool topVoice = false) {
    auto mr = midi::parse_file(path);
    if (!mr.error.empty())
        throw std::runtime_error("MIDI parse error: " + mr.error);

    // Pick track: use specified index, or find first track with notes
    int ti = trackIdx;
    if (ti < 0) {
        for (int t = 0; t < int(mr.tracks.size()); ++t) {
            if (!mr.tracks[t].notes.empty()) { ti = t; break; }
        }
    }
    if (ti < 0 || ti >= int(mr.tracks.size()) || mr.tracks[ti].notes.empty())
        throw std::runtime_error("No notes found in MIDI file");

    auto& rawNotes = mr.tracks[ti].notes;

    // Top voice filter: at each time point, keep only the highest pitch
    std::vector<midi::NoteEvent> filteredNotes;
    if (topVoice && !rawNotes.empty()) {
        // Sort by start beat, then pitch descending
        std::vector<const midi::NoteEvent*> sorted;
        for (auto& n : rawNotes) sorted.push_back(&n);
        std::sort(sorted.begin(), sorted.end(),
            [](const midi::NoteEvent* a, const midi::NoteEvent* b) {
                if (std::abs(a->start_beat - b->start_beat) < 0.01)
                    return a->pitch > b->pitch;  // higher pitch first
                return a->start_beat < b->start_beat;
            });

        double lastStart = -999;
        for (auto* n : sorted) {
            // Skip notes that start at the same time as one we already kept
            // (we kept the higher one since we sorted pitch descending)
            if (std::abs(n->start_beat - lastStart) < 0.01)
                continue;
            filteredNotes.push_back(*n);
            lastStart = n->start_beat;
        }
        std::cerr << "    (top-voice filter: " << rawNotes.size()
                  << " → " << filteredNotes.size() << " notes)\n";
    } else {
        filteredNotes = rawNotes;
    }
    auto& notes = filteredNotes;

    // Use specified key, or default to C major
    // TODO: Krumhansl-Schmuckler pitch histogram analysis for auto-detection
    int rootPitch = (keyRoot >= 0) ? keyRoot : 0;
    auto scale = keyMinor ? midi::ScaleMap::natural_minor(rootPitch)
                          : midi::ScaleMap::major(rootPitch);

    // Detect starting degree
    int startDeg = 0;
    if (!notes.empty())
        startDeg = scale.absolute_degree(notes[0].pitch) % 7;

    // Meter from MIDI time signature
    std::string meter = "4/4";
    double bpb = 4.0;
    if (!mr.time_sigs.empty()) {
        int num = mr.time_sigs[0].numerator;
        int den = 1 << mr.time_sigs[0].denominator_power;
        meter = std::to_string(num) + "/" + std::to_string(den);
        bpb = double(num) * (4.0 / double(den));
    }

    // Tempo
    float bpm = 120.0f;
    if (!mr.tempos.empty())
        bpm = 60000000.0f / float(mr.tempos[0].us_per_quarter);

    static const char* noteNames[] = {"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
    std::string scName = keyMinor ? "Minor" : "Major";
    std::string header = std::string("key:") + noteNames[rootPitch % 12] +
                         " scale:" + scName + " meter:" + meter +
                         " bpm:" + std::to_string(int(bpm + 0.5f)) +
                         " start:" + std::to_string(startDeg);

    // Convert notes to generic units, detecting chromatic vs diatonic movement.
    // A chromatic note is one whose pitch doesn't land on a scale degree.
    std::vector<GenericUnit> gunits;
    int prevMidi = -1;
    bool first = true;
    double prevEnd = 0;
    for (auto& n : notes) {
        // Insert rest for gap
        double gap = n.start_beat - prevEnd;
        if (gap > 0.1 && !first)
            gunits.push_back({float(gap), 0, true, false});

        if (first) {
            // Check if first note needs accidental
            int semiInOctave = ((n.pitch - rootPitch) % 12 + 12) % 12;
            bool isDiatonic = false;
            for (int s : scale.semitone_offsets) {
                if (s == semiInOctave) { isDiatonic = true; break; }
            }
            int acc = 0;
            if (!isDiatonic) {
                // Find nearest scale degree and compute accidental
                int nearestSemi = scale.semitone_offsets[0];
                for (int s : scale.semitone_offsets) {
                    if (std::abs(s - semiInOctave) < std::abs(nearestSemi - semiInOctave))
                        nearestSemi = s;
                }
                acc = semiInOctave - nearestSemi;
            }
            gunits.push_back({float(n.duration_beats), 0, false, acc});
            first = false;
        } else {
            // Compute diatonic step (snap both pitches to scale degrees)
            int curDeg = scale.absolute_degree(n.pitch);
            int prevDeg = scale.absolute_degree(prevMidi);
            int step = curDeg - prevDeg;

            // Check if this note needs an accidental
            int semiInOctave = ((n.pitch - rootPitch) % 12 + 12) % 12;
            bool isDiatonic = false;
            for (int s : scale.semitone_offsets) {
                if (s == semiInOctave) { isDiatonic = true; break; }
            }
            int acc = 0;
            if (!isDiatonic) {
                int nearestSemi = scale.semitone_offsets[0];
                for (int s : scale.semitone_offsets) {
                    if (std::abs(s - semiInOctave) < std::abs(nearestSemi - semiInOctave))
                        nearestSemi = s;
                }
                acc = semiInOctave - nearestSemi;
            }
            gunits.push_back({float(n.duration_beats), step, false, acc});
        }
        prevMidi = n.pitch;
        prevEnd = n.start_beat + n.duration_beats;
    }

    std::string trackInfo = mr.tracks[ti].name.empty()
        ? "track " + std::to_string(ti)
        : mr.tracks[ti].name;
    std::cerr << "    (MIDI: " << notes.size() << " notes from " << trackInfo
              << ", " << bpm << " bpm)\n";

    return emit_durn(header, gunits, bpb);
}

// ---------------------------------------------------------------------------
// MusicXML → DURN
// ---------------------------------------------------------------------------
static std::string musicxml_to_durn(const std::string& path) {
    auto result = mxml::parse_file(path);

    if (result.figureUnits.empty())
        throw std::runtime_error("No notes found in MusicXML file");

    auto scaleMap = mxml::ScaleMap::from_key(result.key);
    static const char* noteNamesSharp[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
    static const char* noteNamesFlat[]  = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
    const char* keyName = (result.key.fifths >= 0)
        ? noteNamesSharp[scaleMap.rootMidi % 12]
        : noteNamesFlat[scaleMap.rootMidi % 12];
    std::string scaleName = (result.key.mode == "minor") ? "Minor" : "Major";
    std::string meter = std::to_string(result.time.beats) + "/" +
                        std::to_string(result.time.beatType);
    double bpb = double(result.time.beats) * (4.0 / double(result.time.beatType));

    // Detect starting degree
    int startDeg = 0;
    for (auto& n : result.notes) {
        if (!n.isRest) {
            startDeg = scaleMap.midi_to_degree(n.midi_note()) % 7;
            if (startDeg < 0) startDeg = 0;
            break;
        }
    }

    std::string header = std::string("key:") + keyName +
                         " scale:" + scaleName +
                         " meter:" + meter + " bpm:120" +
                         " start:" + std::to_string(startDeg);

    // Convert — MusicXML figureUnits parallel the merged note list
    std::vector<GenericUnit> gunits;
    for (auto& fu : result.figureUnits)
        gunits.push_back({fu.duration, fu.step, false});

    return emit_durn(header, gunits, bpb);
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
        std::cerr << "Usage: durn_converter <raw_dir> --genre <genre> --out <durn_dir> [--all] [--key <note>[m]]\n"
                  << "  Converts ABC/kern/MIDI/MusicXML files to DURN format.\n"
                  << "  --new (default): skip files already converted\n"
                  << "  --all: reconvert everything\n"
                  << "  --key A    : specify key for MIDI (A major)\n"
                  << "  --key Am   : specify key for MIDI (A minor)\n"
                  << "  --key F#m  : F# minor, etc.\n";
        return 1;
    }

    std::string rawDir;
    std::string genre;
    std::string outDir;
    bool convertAll = false;
    std::string keyOverride;  // empty = auto-detect / default
    bool topVoice = false;

    rawDir = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--genre" && i + 1 < argc) genre = argv[++i];
        else if (arg == "--out" && i + 1 < argc) outDir = argv[++i];
        else if (arg == "--key" && i + 1 < argc) keyOverride = argv[++i];
        else if (arg == "--all") convertAll = true;
        else if (arg == "--new") convertAll = false;
        else if (arg == "--top-voice") topVoice = true;
    }

    // Parse key override into root pitch class + major/minor
    int keyRoot = -1;   // -1 = not specified
    bool keyMinor = false;
    if (!keyOverride.empty()) {
        static const std::pair<std::string, int> noteMap[] = {
            {"C",0},{"C#",1},{"Db",1},{"D",2},{"D#",3},{"Eb",3},
            {"E",4},{"F",5},{"F#",6},{"Gb",6},{"G",7},{"G#",8},
            {"Ab",8},{"A",9},{"A#",10},{"Bb",10},{"B",11}
        };
        std::string k = keyOverride;
        if (!k.empty() && k.back() == 'm') {
            keyMinor = true;
            k.pop_back();
        }
        for (auto& [name, pc] : noteMap) {
            if (k == name) { keyRoot = pc; break; }
        }
        if (keyRoot < 0)
            std::cerr << "Warning: unrecognized key '" << keyOverride << "', using default\n";
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

        // Handle ABC, kern, MIDI, MusicXML
        if (ext != ".abc" && ext != ".krn" && ext != ".mid" && ext != ".xml") continue;

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
            } else if (ext == ".krn") {
                durn = kern_to_durn(entry.path().string());
            } else if (ext == ".mid") {
                durn = midi_to_durn(entry.path().string(), -1, keyRoot, keyMinor, topVoice);
            } else if (ext == ".xml") {
                durn = musicxml_to_durn(entry.path().string());
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
