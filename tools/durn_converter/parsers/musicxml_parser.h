#pragma once
// ============================================================================
// musicxml_parser.h — Header-only MusicXML melody extractor
// Namespace: mxml
// Requires: C++17, standard library only
// ============================================================================

#ifndef MXML_MUSICXML_PARSER_H
#define MXML_MUSICXML_PARSER_H

#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace mxml {

// ============================================================================
// Minimal hand-rolled XML helpers
// ============================================================================

// Find the content between the first <tag ...> and </tag>.
// Returns the inner text/xml. Returns "" if not found.
inline std::string find_element(const std::string& xml, const std::string& tag) {
    // Match <tag> or <tag ...attrs...>
    std::string open_exact = "<" + tag + ">";
    std::string open_attr  = "<" + tag + " ";
    std::string close_tag  = "</" + tag + ">";

    auto try_find = [&](const std::string& opener) -> std::string {
        auto pos = xml.find(opener);
        if (pos == std::string::npos) return {};
        auto content_start = xml.find('>', pos);
        if (content_start == std::string::npos) return {};
        content_start += 1;
        auto content_end = xml.find(close_tag, content_start);
        if (content_end == std::string::npos) return {};
        return xml.substr(content_start, content_end - content_start);
    };

    // Try exact match first, then attribute match
    std::string result = try_find(open_exact);
    if (!result.empty()) return result;

    // Try with attributes
    auto pos_attr = xml.find(open_attr);
    auto pos_exact = xml.find(open_exact);
    // Pick whichever appears first
    if (pos_attr != std::string::npos &&
        (pos_exact == std::string::npos || pos_attr < pos_exact)) {
        return try_find(open_attr);
    }
    if (pos_exact != std::string::npos) {
        return try_find(open_exact);
    }
    return {};
}

// Find all occurrences of <tag>...</tag> in the xml string.
inline std::vector<std::string> find_all_elements(const std::string& xml,
                                                   const std::string& tag) {
    std::vector<std::string> results;
    std::string open_exact = "<" + tag + ">";
    std::string open_attr  = "<" + tag + " ";
    std::string close_tag  = "</" + tag + ">";

    size_t search_from = 0;
    while (search_from < xml.size()) {
        // Find next opening tag (either exact or with attributes)
        auto pos_exact = xml.find(open_exact, search_from);
        auto pos_attr  = xml.find(open_attr, search_from);

        size_t pos = std::string::npos;
        if (pos_exact != std::string::npos && pos_attr != std::string::npos)
            pos = std::min(pos_exact, pos_attr);
        else if (pos_exact != std::string::npos)
            pos = pos_exact;
        else if (pos_attr != std::string::npos)
            pos = pos_attr;
        else
            break;

        // Find the end of the opening tag
        auto content_start = xml.find('>', pos);
        if (content_start == std::string::npos) break;
        content_start += 1;

        // Check for self-closing tag
        if (xml[content_start - 2] == '/') {
            // Self-closing: <tag ... />, no inner content
            search_from = content_start;
            continue;
        }

        auto content_end = xml.find(close_tag, content_start);
        if (content_end == std::string::npos) break;

        results.push_back(xml.substr(content_start, content_end - content_start));
        search_from = content_end + close_tag.size();
    }

    return results;
}

// Get the text content of a direct child element.
// Equivalent to find_element but trims whitespace.
inline std::string child_text(const std::string& xml, const std::string& tag) {
    std::string raw = find_element(xml, tag);
    // Trim leading/trailing whitespace
    auto start = raw.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return {};
    auto end = raw.find_last_not_of(" \t\r\n");
    return raw.substr(start, end - start + 1);
}

// Check if a tag is present (including self-closing tags like <rest/>).
inline bool has_element(const std::string& xml, const std::string& tag) {
    std::string open_exact = "<" + tag + ">";
    std::string open_attr  = "<" + tag + " ";
    std::string self_close = "<" + tag + "/>";
    if (xml.find(open_exact) != std::string::npos) return true;
    if (xml.find(open_attr) != std::string::npos) return true;
    if (xml.find(self_close) != std::string::npos) return true;
    return false;
}

// Extract an attribute value from a tag string, e.g. get "start" from
// <tie type="start"/>. Searches within xml for <tag ...attr="value"...>
// and returns value.
inline std::string get_attribute(const std::string& xml,
                                  const std::string& tag,
                                  const std::string& attr) {
    // Find the tag opening
    std::string open_tag = "<" + tag;
    size_t search_from = 0;
    while (search_from < xml.size()) {
        auto pos = xml.find(open_tag, search_from);
        if (pos == std::string::npos) return {};

        // Ensure it's actually the tag (followed by space, >, or /)
        auto after = pos + open_tag.size();
        if (after < xml.size() &&
            xml[after] != ' ' && xml[after] != '>' &&
            xml[after] != '/' && xml[after] != '\t' && xml[after] != '\n') {
            search_from = after;
            continue;
        }

        // Find end of this opening tag
        auto tag_end = xml.find('>', pos);
        if (tag_end == std::string::npos) return {};

        std::string tag_str = xml.substr(pos, tag_end - pos + 1);

        // Look for attr="value" or attr='value'
        std::string attr_eq = attr + "=\"";
        auto attr_pos = tag_str.find(attr_eq);
        if (attr_pos != std::string::npos) {
            auto val_start = attr_pos + attr_eq.size();
            auto val_end = tag_str.find('"', val_start);
            if (val_end != std::string::npos)
                return tag_str.substr(val_start, val_end - val_start);
        }

        // Try single quotes
        attr_eq = attr + "='";
        attr_pos = tag_str.find(attr_eq);
        if (attr_pos != std::string::npos) {
            auto val_start = attr_pos + attr_eq.size();
            auto val_end = tag_str.find('\'', val_start);
            if (val_end != std::string::npos)
                return tag_str.substr(val_start, val_end - val_start);
        }

        search_from = tag_end;
    }
    return {};
}

// Find all occurrences of a potentially self-closing or open tag,
// returning the full tag string for attribute extraction.
// Useful for <tie type="start"/>.
inline std::vector<std::string> find_all_tags(const std::string& xml,
                                               const std::string& tag) {
    std::vector<std::string> results;
    std::string open_tag = "<" + tag;
    size_t search_from = 0;

    while (search_from < xml.size()) {
        auto pos = xml.find(open_tag, search_from);
        if (pos == std::string::npos) break;

        auto after = pos + open_tag.size();
        if (after < xml.size() &&
            xml[after] != ' ' && xml[after] != '>' &&
            xml[after] != '/' && xml[after] != '\t' && xml[after] != '\n') {
            search_from = after;
            continue;
        }

        auto tag_end = xml.find('>', pos);
        if (tag_end == std::string::npos) break;

        results.push_back(xml.substr(pos, tag_end - pos + 1));
        search_from = tag_end + 1;
    }
    return results;
}

// ============================================================================
// Data structures
// ============================================================================

struct MxmlPitch {
    char step{'C'};
    int octave{4};
    int alter{0}; // -1=flat, 0=natural, +1=sharp
};

struct MxmlNote {
    MxmlPitch pitch;
    int duration{1};        // in divisions
    bool isRest{false};
    bool isDotted{false};
    bool hasTieStart{false};
    bool hasTieStop{false};
    bool isChordMember{false};
    int voice{1};
    std::string type; // "quarter", "eighth", etc.

    // Compute MIDI note number from pitch.
    // C4 = 60. step letters map: C=0, D=2, E=4, F=5, G=7, A=9, B=11
    int midi_note() const {
        if (isRest) return -1;
        int semitone = 0;
        switch (pitch.step) {
            case 'C': semitone = 0;  break;
            case 'D': semitone = 2;  break;
            case 'E': semitone = 4;  break;
            case 'F': semitone = 5;  break;
            case 'G': semitone = 7;  break;
            case 'A': semitone = 9;  break;
            case 'B': semitone = 11; break;
            default:  semitone = 0;  break;
        }
        return (pitch.octave + 1) * 12 + semitone + pitch.alter;
    }
};

struct MxmlKey {
    int fifths{0};
    std::string mode{"major"};
};

struct MxmlTime {
    int beats{4};
    int beatType{4};
};

struct FigureUnit {
    float duration; // in beats
    int step;       // scale-degree delta from previous note
};

// ============================================================================
// ScaleMap — maps MIDI notes to scale degrees
// ============================================================================

struct ScaleMap {
    int rootMidi{0};                // MIDI note of the scale root (e.g. 60 for C4)
    std::vector<int> intervals;     // semitone offsets within one octave

    // Build a ScaleMap from an MxmlKey.
    // fifths gives the key: 0=C, 1=G, -1=F, etc.
    // mode gives major or minor intervals.
    static ScaleMap from_key(const MxmlKey& key) {
        ScaleMap sm;

        // Major scale intervals
        static const std::vector<int> major_intervals = {0, 2, 4, 5, 7, 9, 11};
        // Natural minor scale intervals
        static const std::vector<int> minor_intervals = {0, 2, 3, 5, 7, 8, 10};

        // Map fifths to root pitch class (semitones above C)
        // fifths: ...-2=Bb, -1=F, 0=C, 1=G, 2=D, 3=A, 4=E, 5=B, 6=F#...
        // Each fifth adds 7 semitones mod 12
        int root_pc = ((key.fifths * 7) % 12 + 12) % 12;

        // Root MIDI: use octave 0 as reference (C0 = 12)
        // We just need the pitch class; octave doesn't matter for degree mapping
        sm.rootMidi = root_pc; // pitch class only (0-11)

        if (key.mode == "minor")
            sm.intervals = minor_intervals;
        else
            sm.intervals = major_intervals;

        return sm;
    }

    // Convert a MIDI note number to an absolute scale degree.
    // Returns octave * 7 + degree_within_scale.
    // If the note is not exactly on a scale tone, picks the nearest.
    int midi_to_degree(int midi) const {
        if (intervals.empty()) return 0;

        // Semitones above root
        int semitones_from_root = midi - rootMidi;

        // Compute octave relative to root
        int octave = 0;
        int remainder = semitones_from_root % 12;
        if (semitones_from_root >= 0) {
            octave = semitones_from_root / 12;
        } else {
            // Handle negative values: floor division
            octave = (semitones_from_root - 11) / 12;
            remainder = ((semitones_from_root % 12) + 12) % 12;
        }

        // Find the nearest scale degree for this remainder
        int best_degree = 0;
        int best_dist = 99;
        for (int i = 0; i < static_cast<int>(intervals.size()); ++i) {
            int dist = std::abs(intervals[i] - remainder);
            // Also check wrapping (e.g. note just below root)
            int dist_wrap = 12 - dist;
            int d = std::min(dist, dist_wrap);
            if (d < best_dist) {
                best_dist = d;
                best_degree = i;
            }
        }

        return octave * static_cast<int>(intervals.size()) + best_degree;
    }
};

// ============================================================================
// ParseResult
// ============================================================================

struct ParseResult {
    MxmlKey key;
    MxmlTime time;
    std::vector<MxmlNote> notes;
    std::vector<FigureUnit> figureUnits;
    std::string title;
    int divisions{1};

    std::string keyDescription() const {
        // Map fifths to key name
        static const char* major_keys[] = {
            "Cb", "Gb", "Db", "Ab", "Eb", "Bb", "F",
            "C", "G", "D", "A", "E", "B", "F#", "C#"
        };
        static const char* minor_keys[] = {
            "Ab", "Eb", "Bb", "F", "C", "G", "D",
            "A", "E", "B", "F#", "C#", "G#", "D#", "A#"
        };
        int idx = key.fifths + 7; // offset so -7 maps to index 0
        if (idx < 0) idx = 0;
        if (idx > 14) idx = 14;

        std::string name;
        if (key.mode == "minor")
            name = minor_keys[idx];
        else
            name = major_keys[idx];

        return name + " " + key.mode;
    }

    std::string timeDescription() const {
        return std::to_string(time.beats) + "/" + std::to_string(time.beatType);
    }
};

// ============================================================================
// Note parsing from a single <note>...</note> block
// ============================================================================

inline MxmlNote parse_note_element(const std::string& note_xml) {
    MxmlNote note;

    // Rest?
    note.isRest = has_element(note_xml, "rest");

    // Chord member?
    note.isChordMember = has_element(note_xml, "chord");

    // Dotted?
    note.isDotted = has_element(note_xml, "dot");

    // Duration
    std::string dur_str = child_text(note_xml, "duration");
    if (!dur_str.empty()) {
        note.duration = std::stoi(dur_str);
    }

    // Voice
    std::string voice_str = child_text(note_xml, "voice");
    if (!voice_str.empty()) {
        note.voice = std::stoi(voice_str);
    }

    // Type (quarter, eighth, etc.)
    note.type = child_text(note_xml, "type");

    // Pitch
    if (!note.isRest && has_element(note_xml, "pitch")) {
        std::string pitch_xml = find_element(note_xml, "pitch");
        std::string step_str = child_text(pitch_xml, "step");
        if (!step_str.empty()) {
            note.pitch.step = step_str[0];
        }
        std::string oct_str = child_text(pitch_xml, "octave");
        if (!oct_str.empty()) {
            note.pitch.octave = std::stoi(oct_str);
        }
        std::string alter_str = child_text(pitch_xml, "alter");
        if (!alter_str.empty()) {
            note.pitch.alter = std::stoi(alter_str);
        }
    }

    // Ties — look for <tie type="start"/> and <tie type="stop"/>
    auto tie_tags = find_all_tags(note_xml, "tie");
    for (auto& t : tie_tags) {
        // Extract type attribute manually from the tag string
        auto type_pos = t.find("type=\"");
        if (type_pos != std::string::npos) {
            auto val_start = type_pos + 6;
            auto val_end = t.find('"', val_start);
            if (val_end != std::string::npos) {
                std::string val = t.substr(val_start, val_end - val_start);
                if (val == "start") note.hasTieStart = true;
                if (val == "stop")  note.hasTieStop = true;
            }
        }
    }

    return note;
}

// ============================================================================
// Core parse function: parse XML string
// ============================================================================

inline ParseResult parse(const std::string& xml) {
    ParseResult result;

    // Title: look in <work><work-title> or <movement-title>
    std::string work_title = child_text(find_element(xml, "work"), "work-title");
    if (!work_title.empty()) {
        result.title = work_title;
    } else {
        std::string mov_title = child_text(xml, "movement-title");
        if (!mov_title.empty()) {
            result.title = mov_title;
        }
    }

    // Find the first <part>
    std::string part_xml = find_element(xml, "part");
    if (part_xml.empty()) return result;

    // Extract all measures
    auto measures = find_all_elements(part_xml, "measure");

    for (auto& measure_xml : measures) {
        // Check for <attributes> to get key, time, divisions
        if (has_element(measure_xml, "attributes")) {
            std::string attr_xml = find_element(measure_xml, "attributes");

            // Divisions
            std::string div_str = child_text(attr_xml, "divisions");
            if (!div_str.empty()) {
                result.divisions = std::stoi(div_str);
                if (result.divisions <= 0) result.divisions = 1;
            }

            // Key signature
            if (has_element(attr_xml, "key")) {
                std::string key_xml = find_element(attr_xml, "key");
                std::string fifths_str = child_text(key_xml, "fifths");
                if (!fifths_str.empty()) {
                    result.key.fifths = std::stoi(fifths_str);
                }
                std::string mode_str = child_text(key_xml, "mode");
                if (!mode_str.empty()) {
                    result.key.mode = mode_str;
                }
            }

            // Time signature
            if (has_element(attr_xml, "time")) {
                std::string time_xml = find_element(attr_xml, "time");
                std::string beats_str = child_text(time_xml, "beats");
                if (!beats_str.empty()) {
                    result.time.beats = std::stoi(beats_str);
                }
                std::string bt_str = child_text(time_xml, "beat-type");
                if (!bt_str.empty()) {
                    result.time.beatType = std::stoi(bt_str);
                }
            }
        }

        // Parse all <note> elements in this measure
        auto note_elements = find_all_elements(measure_xml, "note");
        for (auto& note_xml : note_elements) {
            MxmlNote note = parse_note_element(note_xml);
            result.notes.push_back(note);
        }
    }

    return result;
}

// ============================================================================
// Parse from file
// ============================================================================

inline ParseResult parse_file(const std::string& filepath) {
    std::ifstream ifs(filepath);
    if (!ifs.is_open()) {
        ParseResult empty;
        empty.title = "[Error: could not open " + filepath + "]";
        return empty;
    }
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return parse(ss.str());
}

// ============================================================================
// Tie merging helper
// ============================================================================

// Merge consecutive tied notes of the same pitch into one longer note.
// Returns a new vector with tied notes collapsed.
inline std::vector<MxmlNote> merge_ties(const std::vector<MxmlNote>& notes) {
    std::vector<MxmlNote> merged;
    if (notes.empty()) return merged;

    merged.push_back(notes[0]);

    for (size_t i = 1; i < notes.size(); ++i) {
        const MxmlNote& curr = notes[i];
        MxmlNote& prev = merged.back();

        // If current note is a tie-stop continuation of the previous note
        // (same pitch, previous had tie-start, current has tie-stop)
        if (!curr.isRest && !prev.isRest &&
            curr.hasTieStop &&
            prev.hasTieStart &&
            curr.midi_note() == prev.midi_note()) {
            // Merge: add duration to previous
            prev.duration += curr.duration;
            // Carry forward the tie-start if this note also starts a new tie
            prev.hasTieStart = curr.hasTieStart;
            prev.hasTieStop = false; // merged note is no longer a stop
        } else {
            merged.push_back(curr);
        }
    }

    return merged;
}

// ============================================================================
// Convert notes to FigureUnits
// ============================================================================

inline void convert_to_figure_units(ParseResult& result) {
    result.figureUnits.clear();

    // Filter to voice 1 only, skip chord members
    std::vector<MxmlNote> filtered;
    for (auto& n : result.notes) {
        if (n.voice == 1 && !n.isChordMember) {
            filtered.push_back(n);
        }
    }

    // Merge tied notes
    std::vector<MxmlNote> merged = merge_ties(filtered);

    if (merged.empty()) return;

    // Build scale map from key
    ScaleMap scale = ScaleMap::from_key(result.key);

    int prev_degree = 0;
    bool have_prev = false;

    for (auto& note : merged) {
        FigureUnit fu;

        // Duration in beats = duration_in_divisions / divisions
        fu.duration = static_cast<float>(note.duration) /
                      static_cast<float>(result.divisions);

        if (note.isRest) {
            fu.step = 0;
        } else {
            int midi = note.midi_note();
            int degree = scale.midi_to_degree(midi);

            if (!have_prev) {
                fu.step = 0;
                have_prev = true;
            } else {
                fu.step = degree - prev_degree;
            }
            prev_degree = degree;
        }

        result.figureUnits.push_back(fu);
    }
}

} // namespace mxml

#endif // MXML_MUSICXML_PARSER_H
