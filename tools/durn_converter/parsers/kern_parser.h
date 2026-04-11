#pragma once
// kern_parser.h -- Header-only Humdrum **kern parser
// Self-contained, no engine dependencies. C++17.

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace kern {

// ============================================================
//  Data types
// ============================================================

struct KernNote {
    int   midi_pitch      = 0;      // MIDI note number (60 = middle C)
    float duration_beats  = 0.0f;   // in quarter-note beats
    bool  is_rest         = false;
    bool  is_tied         = false;
    int   pitch_class     = 0;      // 0-11
    int   octave          = 4;
    int   accidental      = 0;      // -1=flat, 0=natural, +1=sharp
    std::string raw;                // original token
};

struct KernMetadata {
    std::string title;
    std::string composer;
    std::string key;
    std::string meter;
    int spine_count = 0;
};

struct FigureUnit {
    float duration = 0.0f;
    int   step     = 0;
};

struct ScaleMap {
    std::string    name;
    int            root_pitch_class = 0;   // 0=C .. 11=B
    std::vector<int> semitone_table;       // cumulative, e.g. {0,2,4,5,7,9,11}

    // ---- factories ----

    static ScaleMap from_steps(const std::string& nm, int root_pc,
                               const std::vector<int>& steps) {
        ScaleMap sm;
        sm.name             = nm;
        sm.root_pitch_class = root_pc % 12;
        sm.semitone_table.reserve(steps.size() + 1);
        sm.semitone_table.push_back(0);
        int acc = 0;
        for (int s : steps) {
            acc += s;
            sm.semitone_table.push_back(acc);
        }
        return sm;
    }

    static ScaleMap major(int root_pc) {
        return from_steps("major", root_pc, {2, 2, 1, 2, 2, 2, 1});
    }

    static ScaleMap minor(int root_pc) {
        return from_steps("minor", root_pc, {2, 1, 2, 2, 1, 2, 2});
    }

    int degree_count() const {
        return static_cast<int>(semitone_table.size()) - 1;
    }

    struct DegreeResult {
        int degree;              // 0-based scale degree within the octave
        int octave_offset;       // octave relative to root
        int chromatic_remainder; // semitones not accounted for by the degree
    };

    // Map a MIDI pitch to a scale degree + octave offset.
    DegreeResult pitch_to_degree(int midi_pitch) const {
        int dc = degree_count();
        if (dc <= 0) return {0, 0, 0};

        int octave_semis = semitone_table.back(); // normally 12
        int rel = midi_pitch - root_pitch_class;  // semitones above root pc
        // Normalise so that rel is non-negative for the modular arithmetic
        // but we need to preserve the octave offset.
        int oct = 0;
        while (rel < 0)            { rel += octave_semis; --oct; }
        while (rel >= octave_semis) { rel -= octave_semis; ++oct; }

        // Find the closest degree (equal or just below)
        int best_deg = 0;
        for (int d = dc - 1; d >= 0; --d) {
            if (semitone_table[d] <= rel) {
                best_deg = d;
                break;
            }
        }

        int remainder = rel - semitone_table[best_deg];
        return {best_deg, oct, remainder};
    }

    // Return an absolute (unbounded) degree number: octave * degree_count + degree
    int absolute_degree(int midi_pitch) const {
        auto dr = pitch_to_degree(midi_pitch);
        return dr.octave_offset * degree_count() + dr.degree;
    }
};

enum class VoiceSelect { First, Last };
enum class RestHandling { Skip, AsZeroStep };

struct KernFile {
    KernMetadata          metadata;
    std::vector<KernNote> melody;
};

struct ConversionResult {
    KernMetadata           metadata;
    std::vector<FigureUnit> units;
    int notes_parsed    = 0;
    int rests_skipped   = 0;
    int ties_merged     = 0;
    int chromatic_notes = 0;
};

// ============================================================
//  Internal helpers
// ============================================================

namespace detail {

inline int letter_to_pc(char c) {
    switch (c) {
        case 'c': case 'C': return 0;
        case 'd': case 'D': return 2;
        case 'e': case 'E': return 4;
        case 'f': case 'F': return 5;
        case 'g': case 'G': return 7;
        case 'a': case 'A': return 9;
        case 'b': case 'B': return 11;
        default:            return -1;
    }
}

inline bool is_pitch_letter(char c) {
    return letter_to_pc(c) >= 0;
}

// Split a line by tabs.
inline std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream ss(line);
    std::string tok;
    while (std::getline(ss, tok, '\t')) {
        tokens.push_back(tok);
    }
    return tokens;
}

// Trim leading/trailing whitespace.
inline std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// Parse the rhythmic value from a kern token.
// Returns duration in quarter-note beats.
inline float parse_duration(const std::string& token) {
    // Skip leading non-digit characters (phrase marks, slurs, etc.)
    size_t start = 0;
    while (start < token.size() && !std::isdigit(static_cast<unsigned char>(token[start])))
        ++start;

    // Find the digits
    size_t i = start;
    while (i < token.size() && std::isdigit(static_cast<unsigned char>(token[i])))
        ++i;

    if (i == start) return 1.0f; // no number found, default to quarter

    int recip = std::atoi(token.substr(start, i - start).c_str());
    if (recip <= 0) return 4.0f; // breve-like fallback

    // Base duration in quarter-note beats: a quarter = recip 4 => 1 beat.
    // recip means "reciprocal of a whole note", so duration_whole = 1/recip,
    // and in quarter-note beats that is 4/recip.
    float dur = 4.0f / static_cast<float>(recip);

    // Count dots in the remainder of the token.
    int dots = 0;
    for (size_t j = i; j < token.size(); ++j) {
        if (token[j] == '.') ++dots;
    }

    // Each dot adds half the previous value.
    float add = dur;
    for (int d = 0; d < dots; ++d) {
        add *= 0.5f;
        dur += add;
    }
    return dur;
}

// Parse pitch from a kern token.  Returns midi pitch, pitch class, octave, accidental.
struct PitchInfo {
    int midi_pitch;
    int pitch_class;
    int octave;
    int accidental;
};

inline PitchInfo parse_pitch(const std::string& token) {
    PitchInfo p{60, 0, 4, 0};

    // Find the first pitch letter
    char letter = 0;
    size_t letter_pos = std::string::npos;
    for (size_t i = 0; i < token.size(); ++i) {
        if (is_pitch_letter(token[i])) {
            letter = token[i];
            letter_pos = i;
            break;
        }
    }
    if (!letter) return p; // shouldn't happen for a valid note token

    bool lower = (letter >= 'a' && letter <= 'z');
    int base_pc = letter_to_pc(letter);

    // Count repeated pitch letters to determine octave.
    int repeat = 0;
    for (size_t i = letter_pos; i < token.size(); ++i) {
        char ch = token[i];
        if ((lower && ch == letter) || (!lower && ch == letter)) {
            ++repeat;
        } else if (is_pitch_letter(ch)) {
            break; // different letter = chord sub-token, stop here
        } else if (ch != '#' && ch != '-' && ch != 'n') {
            // We might hit accidentals after the pitch letters; skip them.
            // Anything else means we're past the pitch section.
            if (!std::isalpha(static_cast<unsigned char>(ch))) continue;
            break;
        }
    }

    // Kern octave rules:
    //   Lowercase c  = C4 (middle C).  cc = C5,  ccc = C6, ...
    //   Uppercase C  = C3.             CC = C2,  CCC = C1, ...
    int octave;
    if (lower) {
        octave = 3 + repeat;  // c=4, cc=5, ...
    } else {
        octave = 4 - repeat;  // C=3, CC=2, ...
    }

    // Accidentals: count # and - after the pitch letters.
    int acc = 0;
    // Scan from after the first pitch letter to the end of the token,
    // looking only at accidental chars that follow the pitch letters.
    bool past_letters = false;
    for (size_t i = letter_pos; i < token.size(); ++i) {
        char ch = token[i];
        if (!past_letters) {
            if (ch == letter) continue; // still counting repeats
            past_letters = true;
        }
        if (ch == '#') ++acc;
        else if (ch == '-') --acc;
        else if (ch == 'n') acc = 0;
        // Stop scanning if we hit something unrelated
        else if (ch != '[' && ch != ']' && ch != '_' && ch != '~'
                 && ch != '(' && ch != ')' && ch != 'L' && ch != 'J'
                 && ch != 'k' && ch != 'K' && ch != '\\' && ch != '/'
                 && ch != ';' && ch != '\'' && ch != '`' && ch != ','
                 && ch != 'q' && ch != 'Q' && ch != 'x' && ch != 'X'
                 && ch != 'y' && ch != 'Y' && ch != 'p' && ch != 'P'
                 && ch != 'T' && ch != 't' && ch != 'M' && ch != 'm'
                 && ch != 'W' && ch != 'w' && ch != 'S' && ch != 's'
                 && ch != 'O' && ch != 'o' && !std::isdigit(static_cast<unsigned char>(ch))
                 && ch != '.') {
            break;
        }
    }

    p.pitch_class = (base_pc + acc + 120) % 12;
    p.octave      = octave;
    p.accidental  = acc;
    p.midi_pitch  = (octave + 1) * 12 + base_pc + acc; // MIDI: C4=60 => (4+1)*12+0=60
    return p;
}

// Is this token a rest?
inline bool is_rest(const std::string& token) {
    // A kern rest contains 'r' after the rhythmic value digits.
    for (size_t i = 0; i < token.size(); ++i) {
        char c = token[i];
        if (c == 'r') return true;
        if (is_pitch_letter(c)) return false;
    }
    return false;
}

inline bool is_grace_note(const std::string& token) {
    // Kern grace notes have 'q' or 'qq' after the pitch letter(s).
    // e.g. "32ggnqLLL", "32aaqJJJ"
    // Don't confuse with note names — 'q' only counts if it follows
    // pitch letters, not if it IS a pitch letter (but 'q' isn't a pitch).
    for (size_t i = 0; i < token.size(); ++i) {
        if (token[i] == 'q' && i > 0) return true;
    }
    return false;
}

// Does this token start or continue a tie?
inline bool is_tie_start(const std::string& token) {
    return token.find('[') != std::string::npos;
}

inline bool is_tie_continue(const std::string& token) {
    return token.find('_') != std::string::npos;
}

inline bool is_tie_end(const std::string& token) {
    return token.find(']') != std::string::npos;
}

// Determine the spine indices that are **kern spines from the
// exclusive interpretation line.
inline std::vector<int> find_kern_spines(const std::string& line) {
    auto cols = split_tabs(line);
    std::vector<int> indices;
    for (int i = 0; i < static_cast<int>(cols.size()); ++i) {
        if (trim(cols[i]) == "**kern") {
            indices.push_back(i);
        }
    }
    return indices;
}

// Parse key interpretation token, e.g. "*G:" => G major, "*c:" => C minor,
// "*F#:" => F# major, "*e-:" => Eb minor.
inline bool parse_key_interp(const std::string& token,
                             int& root_pc, bool& is_minor) {
    // Format: *<letter>[#-]<:>
    if (token.size() < 3) return false;
    if (token[0] != '*') return false;

    size_t i = 1;
    char letter = token[i];
    if (!is_pitch_letter(letter)) return false;

    is_minor = (letter >= 'a' && letter <= 'z');
    root_pc  = letter_to_pc(letter);
    ++i;

    // Optional accidentals
    while (i < token.size() && (token[i] == '#' || token[i] == '-')) {
        if (token[i] == '#') ++root_pc;
        else                 --root_pc;
        ++i;
    }
    root_pc = ((root_pc % 12) + 12) % 12;

    // Expect ':'
    if (i >= token.size() || token[i] != ':') return false;
    return true;
}

// Parse a sub-token that might be part of a multi-stop chord
// (space-separated within a spine cell).
// Returns all pitch tokens.
inline std::vector<std::string> split_substops(const std::string& token) {
    std::vector<std::string> subs;
    std::istringstream ss(token);
    std::string s;
    while (ss >> s) {
        subs.push_back(s);
    }
    return subs;
}

} // namespace detail

// ============================================================
//  Public API
// ============================================================

// Parse a .krn file and extract one voice (spine).
inline KernFile parse_file(const std::string& path,
                           VoiceSelect voice = VoiceSelect::Last) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        throw std::runtime_error("kern::parse_file: cannot open " + path);
    }

    KernFile result;
    std::vector<int> kern_spines;
    bool spines_identified = false;
    int chosen_spine = -1;

    std::string line;
    while (std::getline(ifs, line)) {
        line = detail::trim(line);
        if (line.empty()) continue;

        // ------ comment lines ------
        if (line[0] == '!' && (line.size() < 2 || line[1] != '!')) {
            continue; // local comment
        }
        // Global comments (!!!) -- check for reference records
        if (line.size() >= 3 && line[0] == '!' && line[1] == '!') {
            // Reference records: !!!OTL: title, !!!COM: composer
            if (line.size() > 3 && line[2] == '!') {
                auto colon = line.find(':');
                if (colon != std::string::npos) {
                    std::string key = detail::trim(line.substr(3, colon - 3));
                    std::string val = detail::trim(line.substr(colon + 1));
                    if (key == "OTL") result.metadata.title    = val;
                    if (key == "COM") result.metadata.composer  = val;
                }
            }
            continue;
        }

        // ------ barlines ------
        if (line[0] == '=') continue;

        // ------ exclusive interpretation (start of spines) ------
        if (!spines_identified && line.find("**") != std::string::npos) {
            kern_spines = detail::find_kern_spines(line);
            result.metadata.spine_count = static_cast<int>(
                detail::split_tabs(line).size());
            spines_identified = true;

            if (kern_spines.empty()) {
                throw std::runtime_error(
                    "kern::parse_file: no **kern spines found");
            }

            // Choose which spine
            if (voice == VoiceSelect::First)
                chosen_spine = kern_spines.front();
            else
                chosen_spine = kern_spines.back();

            continue;
        }

        // ------ tandem interpretations ------
        if (line[0] == '*') {
            auto cols = detail::split_tabs(line);
            // Check all columns for key and meter info
            for (auto& col : cols) {
                col = detail::trim(col);
                // Key signature: *G:, *c:, *F#:, etc.
                int rpc = 0;
                bool is_min = false;
                if (detail::parse_key_interp(col, rpc, is_min)) {
                    result.metadata.key = col.substr(1); // strip leading *
                }
                // Meter: *M4/4, *M3/4, etc.
                if (col.size() > 2 && col[0] == '*' && col[1] == 'M'
                    && std::isdigit(static_cast<unsigned char>(col[2]))) {
                    result.metadata.meter = col.substr(2);
                }
            }
            continue;
        }

        // ------ data lines ------
        if (!spines_identified || chosen_spine < 0) continue;

        auto cols = detail::split_tabs(line);
        if (chosen_spine >= static_cast<int>(cols.size())) continue;

        std::string cell = detail::trim(cols[chosen_spine]);
        if (cell.empty() || cell == ".") continue;

        // Handle multi-stop chords: space-separated sub-tokens.
        // We take the first or last sub-token depending on voice.
        auto subs = detail::split_substops(cell);
        if (subs.empty()) continue;

        // For a monophonic melody extraction, take just one sub-token.
        const std::string& token = (voice == VoiceSelect::First)
                                       ? subs.front()
                                       : subs.back();

        // Skip grace notes — they're ornamental, not part of the rhythmic structure
        if (detail::is_grace_note(token)) continue;

        KernNote note;
        note.raw = token;
        note.duration_beats = detail::parse_duration(token);

        if (detail::is_rest(token)) {
            note.is_rest = true;
        } else {
            auto pi = detail::parse_pitch(token);
            note.midi_pitch  = pi.midi_pitch;
            note.pitch_class = pi.pitch_class;
            note.octave      = pi.octave;
            note.accidental  = pi.accidental;

            // A note is "tied" if it's a continuation/end of a tie
            // (meaning we should merge its duration with the previous note).
            note.is_tied = detail::is_tie_continue(token)
                        || detail::is_tie_end(token);
        }

        result.melody.push_back(std::move(note));
    }

    return result;
}

// Merge tied notes: when a note is tied, fold its duration into the
// preceding note and remove it from the list.
inline std::vector<KernNote> merge_ties(const std::vector<KernNote>& notes) {
    std::vector<KernNote> out;
    out.reserve(notes.size());

    for (auto& n : notes) {
        if (n.is_tied && !out.empty() && !out.back().is_rest
            && out.back().midi_pitch == n.midi_pitch) {
            // Merge duration into the previous note
            out.back().duration_beats += n.duration_beats;
        } else {
            out.push_back(n);
        }
    }
    return out;
}

// Detect a scale from kern metadata key string.
inline ScaleMap detect_scale(const KernMetadata& meta) {
    if (meta.key.empty()) {
        return ScaleMap::major(0); // default C major
    }

    // Parse the key string (without leading *), e.g. "G:", "c:", "F#:", "e-:"
    std::string k = meta.key;

    // Ensure it ends with ':'
    size_t colon = k.find(':');
    if (colon == std::string::npos) {
        return ScaleMap::major(0);
    }

    char letter = k[0];
    bool is_minor = (letter >= 'a' && letter <= 'z');
    int root_pc = detail::letter_to_pc(letter);
    if (root_pc < 0) return ScaleMap::major(0);

    // Parse accidentals between letter and colon
    for (size_t i = 1; i < colon; ++i) {
        if (k[i] == '#') ++root_pc;
        else if (k[i] == '-') --root_pc;
    }
    root_pc = ((root_pc % 12) + 12) % 12;

    return is_minor ? ScaleMap::minor(root_pc) : ScaleMap::major(root_pc);
}

// Convert a parsed KernFile into FigureUnits using a scale mapping.
inline ConversionResult to_figure_units(const KernFile& kf,
                                        const ScaleMap& scale,
                                        RestHandling rests = RestHandling::Skip) {
    ConversionResult cr;
    cr.metadata = kf.metadata;

    // First, merge ties
    auto merged = merge_ties(kf.melody);

    // Count ties merged
    cr.ties_merged = static_cast<int>(kf.melody.size()) - static_cast<int>(merged.size());

    int prev_abs_degree = 0;
    bool have_prev = false;

    for (auto& n : merged) {
        if (n.is_rest) {
            if (rests == RestHandling::Skip) {
                ++cr.rests_skipped;
                continue;
            }
            // AsZeroStep: emit a unit with duration but step=0
            FigureUnit fu;
            fu.duration = n.duration_beats;
            fu.step     = 0;
            cr.units.push_back(fu);
            ++cr.rests_skipped;
            continue;
        }

        ++cr.notes_parsed;

        auto dr = scale.pitch_to_degree(n.midi_pitch);
        if (dr.chromatic_remainder != 0) {
            ++cr.chromatic_notes;
        }

        int abs_deg = scale.absolute_degree(n.midi_pitch);

        FigureUnit fu;
        fu.duration = n.duration_beats;

        if (!have_prev) {
            fu.step  = 0; // first note has step 0
            have_prev = true;
        } else {
            fu.step = abs_deg - prev_abs_degree;
        }
        prev_abs_degree = abs_deg;

        cr.units.push_back(fu);
    }

    return cr;
}

// One-shot convenience: parse, detect/override scale, convert.
inline ConversionResult convert_file(const std::string& path,
                                     VoiceSelect voice       = VoiceSelect::Last,
                                     RestHandling rests      = RestHandling::Skip,
                                     std::optional<ScaleMap> override_scale = std::nullopt) {
    KernFile kf = parse_file(path, voice);

    ScaleMap scale = override_scale.has_value()
                         ? override_scale.value()
                         : detect_scale(kf.metadata);

    return to_figure_units(kf, scale, rests);
}

} // namespace kern
