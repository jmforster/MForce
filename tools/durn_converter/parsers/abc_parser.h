#pragma once
// abc_parser.h -- Header-only ABC notation parser (C++17, no engine dependencies)
// Converts ABC text into AbcTune / FigureUnitSequence representations.

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace abc {

// ---------------------------------------------------------------------------
// Forward helpers
// ---------------------------------------------------------------------------

inline int note_name_to_semitone(char n) {
    switch (std::toupper(static_cast<unsigned char>(n))) {
        case 'C': return 0;
        case 'D': return 2;
        case 'E': return 4;
        case 'F': return 5;
        case 'G': return 7;
        case 'A': return 9;
        case 'B': return 11;
        default:  return -1;
    }
}

// ---------------------------------------------------------------------------
// Structs
// ---------------------------------------------------------------------------

struct AbcNote {
    std::string note_name;   // "C","D",..."B" (always uppercase)
    int octave;              // 4 = middle octave (C4 = middle C)
    double duration_beats;   // duration in quarter-note beats
    bool is_rest;
    int accidental;          // -2..-1=flats, 0=natural, +1..+2=sharps
    static constexpr int ACC_DEFAULT = -99; // use key signature default
};

struct KeySignature {
    std::string tonic;       // "C", "D", "Eb", etc.
    std::string mode;        // "major","minor","dorian", etc.

    // Internal: accidental applied to each note letter (C..B) by this key
    std::map<char, int> note_accidentals;
    // Internal: scale intervals (semitone offsets from tonic, length 7)
    std::array<int, 7> scale_intervals{};

    int tonic_semitone() const {
        if (tonic.empty()) return 0;
        int base = note_name_to_semitone(tonic[0]);
        if (base < 0) return 0;
        for (size_t i = 1; i < tonic.size(); ++i) {
            if (tonic[i] == '#' || tonic[i] == 's') ++base;   // "F#" or "Fs"
            else if (tonic[i] == 'b')                --base;
        }
        return ((base % 12) + 12) % 12;
    }

    void build_scale() {
        // Mode interval patterns (semitones between consecutive degrees)
        static const std::map<std::string, std::array<int,7>> mode_steps = {
            {"major",      {2,2,1,2,2,2,1}},
            {"minor",      {2,1,2,2,1,2,2}},
            {"dorian",     {2,1,2,2,2,1,2}},
            {"mixolydian", {2,2,1,2,2,1,2}},
            {"phrygian",   {1,2,2,2,1,2,2}},
            {"lydian",     {2,2,2,1,2,2,1}},
            {"locrian",    {1,2,2,1,2,2,2}},
        };

        auto it = mode_steps.find(mode);
        const auto& steps = (it != mode_steps.end()) ? it->second : mode_steps.at("major");

        scale_intervals[0] = 0;
        for (int i = 1; i < 7; ++i) {
            scale_intervals[i] = scale_intervals[i - 1] + steps[i - 1];
        }

        // Build per-note accidentals by walking the scale on the circle
        note_accidentals.clear();
        static const char letters[] = "CDEFGAB";
        // Default: all natural
        for (char c : std::string("CDEFGAB")) note_accidentals[c] = 0;

        int ts = tonic_semitone();
        // Find the letter index of the tonic
        int tonic_letter_idx = -1;
        for (int i = 0; i < 7; ++i) {
            if (letters[i] == std::toupper(static_cast<unsigned char>(tonic[0]))) {
                tonic_letter_idx = i;
                break;
            }
        }
        if (tonic_letter_idx < 0) tonic_letter_idx = 0;

        // Walk each scale degree and assign the accidental needed
        for (int deg = 0; deg < 7; ++deg) {
            int letter_idx = (tonic_letter_idx + deg) % 7;
            char letter = letters[letter_idx];
            int natural_semi = note_name_to_semitone(letter);
            int target_semi = (ts + scale_intervals[deg]) % 12;
            int diff = target_semi - natural_semi;
            // Normalize to -2..+2
            if (diff > 6)  diff -= 12;
            if (diff < -6) diff += 12;
            note_accidentals[letter] = diff;
        }
    }

    int get_accidental(char note_letter) const {
        char upper = static_cast<char>(std::toupper(static_cast<unsigned char>(note_letter)));
        auto it = note_accidentals.find(upper);
        return (it != note_accidentals.end()) ? it->second : 0;
    }

    int semitone_to_scale_degree(int semitone_offset) const {
        // semitone_offset is 0-11 relative to tonic; find nearest degree 0-6
        int norm = ((semitone_offset % 12) + 12) % 12;
        int best_deg = 0;
        int best_dist = 99;
        for (int d = 0; d < 7; ++d) {
            int dist = std::abs(scale_intervals[d] - norm);
            if (dist > 6) dist = 12 - dist;
            if (dist < best_dist) {
                best_dist = dist;
                best_deg = d;
            }
        }
        return best_deg;
    }
};

struct AbcTune {
    int index = 1;
    std::string title;
    std::string rhythm;
    std::string meter_str;        // "4/4", "6/8" etc.
    std::string default_length;
    std::string key_str;
    KeySignature key;
    double default_note_len = 1.0 / 8.0; // fraction of whole note
    int meter_num = 4, meter_den = 4;
    std::vector<AbcNote> notes;
};

struct FigureUnit {
    double duration;  // beats
    int step;         // scale-degree movement from previous (0 for first)
};

struct FigureUnitSequence {
    std::string source_title;
    std::string key;
    std::string meter;
    std::vector<FigureUnit> units;
};

// ---------------------------------------------------------------------------
// Parser helpers (internal)
// ---------------------------------------------------------------------------
namespace detail {

inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

inline std::string to_lower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
        [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    return r;
}

inline KeySignature parse_key(const std::string& raw) {
    KeySignature ks;
    std::string s = trim(raw);
    if (s.empty()) { ks.tonic = "C"; ks.mode = "major"; ks.build_scale(); return ks; }

    // Tonic: first letter + optional accidental
    size_t pos = 0;
    ks.tonic = std::string(1, static_cast<char>(std::toupper(static_cast<unsigned char>(s[0]))));
    pos = 1;
    while (pos < s.size() && (s[pos] == '#' || s[pos] == 'b')) {
        ks.tonic += s[pos];
        ++pos;
    }

    // Mode (skip whitespace)
    while (pos < s.size() && std::isspace(static_cast<unsigned char>(s[pos]))) ++pos;
    std::string mode_part = to_lower(s.substr(pos));

    // Map abbreviations
    if (mode_part.empty() || mode_part.rfind("maj", 0) == 0) ks.mode = "major";
    else if (mode_part.rfind("min", 0) == 0 || mode_part == "m") ks.mode = "minor";
    else if (mode_part.rfind("dor", 0) == 0) ks.mode = "dorian";
    else if (mode_part.rfind("mix", 0) == 0) ks.mode = "mixolydian";
    else if (mode_part.rfind("phr", 0) == 0) ks.mode = "phrygian";
    else if (mode_part.rfind("lyd", 0) == 0) ks.mode = "lydian";
    else if (mode_part.rfind("loc", 0) == 0) ks.mode = "locrian";
    else ks.mode = "major";

    ks.build_scale();
    return ks;
}

inline void parse_fraction(const std::string& s, int& num, int& den) {
    auto slash = s.find('/');
    if (slash != std::string::npos) {
        num = std::stoi(s.substr(0, slash));
        den = std::stoi(s.substr(slash + 1));
    } else {
        num = std::stoi(s);
        den = 1;
    }
}

} // namespace detail

// ---------------------------------------------------------------------------
// AbcParser
// ---------------------------------------------------------------------------

class AbcParser {
public:
    AbcTune parse(const std::string& text) const {
        AbcTune tune;
        std::istringstream stream(text);
        std::string body;
        std::string line;
        bool in_body = false;

        // Pass 1: extract headers and accumulate body lines
        while (std::getline(stream, line)) {
            // Strip trailing \r
            if (!line.empty() && line.back() == '\r') line.pop_back();

            if (!in_body && line.size() >= 2 && line[1] == ':' && std::isalpha(static_cast<unsigned char>(line[0]))) {
                char hdr = line[0];
                std::string val = detail::trim(line.substr(2));
                switch (hdr) {
                    case 'X': tune.index = std::stoi(val); break;
                    case 'T': tune.title = val; break;
                    case 'R': tune.rhythm = val; break;
                    case 'M':
                        tune.meter_str = val;
                        if (val == "C" || val == "C|") {
                            tune.meter_num = 4; tune.meter_den = 4;
                        } else {
                            detail::parse_fraction(val, tune.meter_num, tune.meter_den);
                        }
                        break;
                    case 'L': {
                        tune.default_length = val;
                        int n, d;
                        detail::parse_fraction(val, n, d);
                        tune.default_note_len = static_cast<double>(n) / d;
                        break;
                    }
                    case 'K':
                        tune.key_str = val;
                        tune.key = detail::parse_key(val);
                        in_body = true; // K: is last header before body
                        break;
                    default: break; // skip unknown headers
                }
            } else {
                // Comment lines
                if (!line.empty() && line[0] == '%') continue;
                in_body = true;
                body += line + " ";
            }
        }

        // If no L: header, derive default note length from meter
        if (tune.default_length.empty()) {
            double meter_val = static_cast<double>(tune.meter_num) / tune.meter_den;
            tune.default_note_len = (meter_val < 0.75) ? (1.0 / 16.0) : (1.0 / 8.0);
        }

        // Pass 2: parse body into notes
        parse_body(body, tune);

        return tune;
    }

private:
    void parse_body(const std::string& body, AbcTune& tune) const {
        size_t i = 0;
        const size_t len = body.size();

        while (i < len) {
            char c = body[i];

            // Skip whitespace and barlines
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '|'
                || c == ':' || c == '[' || c == ']') {
                // Handle [| |] || etc. -- just skip
                ++i; continue;
            }

            // Skip chord annotations "..."
            if (c == '"') {
                ++i;
                while (i < len && body[i] != '"') ++i;
                if (i < len) ++i;
                continue;
            }

            // Skip grace notes {...}
            if (c == '{') {
                while (i < len && body[i] != '}') ++i;
                if (i < len) ++i;
                continue;
            }

            // Skip decorations: ~ or !...!
            if (c == '~') { ++i; continue; }
            if (c == '!' || c == 'H' || c == 'L' || c == 'T') {
                if (c == '!') {
                    ++i;
                    while (i < len && body[i] != '!') ++i;
                    if (i < len) ++i;
                    continue;
                }
                // H, L, T alone are valid note letters only if not decorations.
                // In ABC, H is a decoration shorthand -- but H/L/T aren't note names,
                // so we skip them if they appear outside of a note context.
                // Actually only process A-G/a-g, so these fall through to the skip below.
            }

            // Rest: z or Z
            if (c == 'z' || c == 'Z') {
                ++i;
                double dur = parse_duration(body, i, tune.default_note_len);
                double beats = dur * 4.0; // whole note = 4 beats
                AbcNote note;
                note.is_rest = true;
                note.note_name = "R";
                note.octave = 0;
                note.duration_beats = beats;
                note.accidental = 0;
                tune.notes.push_back(note);
                // Check for tie (nonsensical on rest, but handle gracefully)
                skip_tie(body, i);
                continue;
            }

            // Accidentals before a note
            int acc = AbcNote::ACC_DEFAULT;
            if (c == '^' || c == '_' || c == '=') {
                acc = 0;
                while (i < len && (body[i] == '^' || body[i] == '_' || body[i] == '=')) {
                    if (body[i] == '^') ++acc;
                    else if (body[i] == '_') --acc;
                    else acc = 0; // '=' natural resets
                    ++i;
                }
                if (i >= len) break;
                c = body[i];
            }

            // Note letter
            bool is_note_letter = (c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g');
            if (!is_note_letter) {
                ++i; continue; // skip unrecognized character
            }

            AbcNote note;
            note.is_rest = false;

            bool lower = (c >= 'a' && c <= 'g');
            char upper_c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
            note.note_name = std::string(1, upper_c);

            // Base octave: uppercase = octave 4, lowercase = octave 5
            note.octave = lower ? 5 : 4;
            ++i;

            // Octave modifiers: ' (up), , (down)
            while (i < len && (body[i] == '\'' || body[i] == ',')) {
                if (body[i] == '\'') ++note.octave;
                else                 --note.octave;
                ++i;
            }

            note.accidental = acc;

            // Duration
            double dur = parse_duration(body, i, tune.default_note_len);
            note.duration_beats = dur * 4.0;

            tune.notes.push_back(note);

            // Tie: merge with next note of same pitch
            if (i < len && body[i] == '-') {
                // Mark that we need to tie -- we'll resolve ties in a post-pass
                // For simplicity, parse the tie now: peek ahead and merge
                merge_tied(body, i, tune, tune.default_note_len);
            }
        }
    }

    // Parse duration multiplier at current position; advances i.
    // Returns duration as fraction of whole note.
    double parse_duration(const std::string& body, size_t& i, double base_len) const {
        double multiplier = 1.0;
        const size_t len = body.size();

        // Number before slash: e.g. "3", "3/2", "2"
        if (i < len && std::isdigit(static_cast<unsigned char>(body[i]))) {
            int num = 0;
            while (i < len && std::isdigit(static_cast<unsigned char>(body[i]))) {
                num = num * 10 + (body[i] - '0');
                ++i;
            }
            if (i < len && body[i] == '/') {
                ++i;
                int den = 0;
                if (i < len && std::isdigit(static_cast<unsigned char>(body[i]))) {
                    while (i < len && std::isdigit(static_cast<unsigned char>(body[i]))) {
                        den = den * 10 + (body[i] - '0');
                        ++i;
                    }
                } else {
                    den = 2; // "3/" means "3/2"
                }
                multiplier = static_cast<double>(num) / den;
            } else {
                multiplier = static_cast<double>(num);
            }
        } else if (i < len && body[i] == '/') {
            // "/2" or just "/"
            ++i;
            int den = 0;
            if (i < len && std::isdigit(static_cast<unsigned char>(body[i]))) {
                while (i < len && std::isdigit(static_cast<unsigned char>(body[i]))) {
                    den = den * 10 + (body[i] - '0');
                    ++i;
                }
            } else {
                den = 2;
            }
            multiplier = 1.0 / den;
        }

        return base_len * multiplier;
    }

    void skip_tie(const std::string& body, size_t& i) const {
        while (i < body.size() && body[i] == '-') ++i;
    }

    // Merge tied notes: consume '-' and the next note, adding its duration to the last note.
    void merge_tied(const std::string& body, size_t& i, AbcTune& tune, double base_len) const {
        while (i < body.size() && body[i] == '-') {
            ++i;
            // Skip whitespace and barlines between tie and next note
            while (i < body.size() && (body[i] == ' ' || body[i] == '|' || body[i] == '\t'
                   || body[i] == '\n' || body[i] == '\r' || body[i] == ':'))
                ++i;
            if (i >= body.size()) break;

            // Skip accidentals (tied note may repeat them)
            while (i < body.size() && (body[i] == '^' || body[i] == '_' || body[i] == '='))
                ++i;
            if (i >= body.size()) break;

            char c = body[i];
            bool is_note = (c >= 'A' && c <= 'G') || (c >= 'a' && c <= 'g');
            if (!is_note) break;

            ++i; // consume note letter
            // Skip octave markers
            while (i < body.size() && (body[i] == '\'' || body[i] == ',')) ++i;

            double dur = parse_duration(body, i, base_len);
            if (!tune.notes.empty()) {
                tune.notes.back().duration_beats += dur * 4.0;
            }
        }
    }
};

// ---------------------------------------------------------------------------
// to_figure_units
// ---------------------------------------------------------------------------

inline FigureUnitSequence to_figure_units(const AbcTune& tune) {
    FigureUnitSequence seq;
    seq.source_title = tune.title;
    seq.key = tune.key_str;
    seq.meter = tune.meter_str;

    const auto& key = tune.key;
    int tonic_semi = key.tonic_semitone();
    int prev_degree = -1;
    bool first = true;

    for (const auto& n : tune.notes) {
        FigureUnit fu;
        fu.duration = n.duration_beats;

        if (n.is_rest) {
            fu.step = 0;
            seq.units.push_back(fu);
            continue; // don't update prev_degree for rests
        }

        // Compute absolute semitone
        int base_semi = note_name_to_semitone(n.note_name[0]);
        int acc = n.accidental;
        if (acc == AbcNote::ACC_DEFAULT) {
            acc = key.get_accidental(n.note_name[0]);
        }
        int abs_semi = base_semi + acc + n.octave * 12;

        // Semitone offset from tonic (mod 12)
        int rel_semi = ((abs_semi - tonic_semi) % 12 + 12) % 12;
        int degree = key.semitone_to_scale_degree(rel_semi);

        // Add octave-worth of degrees for absolute degree
        int octave_offset = (abs_semi - tonic_semi) / 12;
        // Correct for negative wrapping
        if ((abs_semi - tonic_semi) < 0 && rel_semi != 0) --octave_offset;
        int abs_degree = degree + octave_offset * 7;

        if (first) {
            fu.step = 0;
            first = false;
        } else {
            fu.step = abs_degree - prev_degree;
        }
        prev_degree = abs_degree;

        seq.units.push_back(fu);
    }

    return seq;
}

} // namespace abc
