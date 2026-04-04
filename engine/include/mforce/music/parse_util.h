#pragma once
#include "mforce/music/basics.h"
#include "mforce/music/structure.h"
#include <string>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <cstring>
#include <cctype>

namespace mforce {

// ---------------------------------------------------------------------------
// Duration parser: single char → beats
// t=1/32, s=1/16, e=1/8, q=1/4, h=1/2, w=1, d=2, f=4 (in beats)
// Second char '.' = dotted (1.5x), anything else = 1.25x
// ---------------------------------------------------------------------------
inline float parse_duration(const std::string& durStr) {
    static const std::string durChars = "tseqhwdf";
    static const float durBeats[] = {0.125f, 0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f, 16.0f};

    if (durStr.empty()) return 4.0f;

    auto pos = durChars.find(durStr[0]);
    if (pos == std::string::npos)
        throw std::runtime_error("Unknown duration char: " + durStr);

    float beats = durBeats[pos];
    if (durStr.size() >= 2) {
        beats *= (durStr[1] == '.') ? 1.5f : 1.25f;
    }
    return beats;
}

// ---------------------------------------------------------------------------
// Chord group shorthand expansion
// ---------------------------------------------------------------------------
inline std::string map_chord_group(const std::string& grp) {
    if (grp == "g4") return "Guitar-Bar-4";
    if (grp == "g5") return "Guitar-Bar-5";
    if (grp == "g6") return "Guitar-Bar-6";
    return grp;
}

// ---------------------------------------------------------------------------
// ParsedChord — result of parsing a chord string token
// ---------------------------------------------------------------------------
struct ParsedChord {
    Chord chord;
    int octave;
};

// ---------------------------------------------------------------------------
// Parse a single chord token like "C:M", "Am:m7", "G:7"
// Format: root:chordType  (root = pitch name, chordType = ChordDef short name)
// ---------------------------------------------------------------------------
inline Chord parse_chord_token(const std::string& token, int octave,
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
// Parse a full chord string (legacy format from SoundController)
// Format: "Em7_d F#7#9_g6_q. O+ Cmu_hh O-"
// ---------------------------------------------------------------------------
inline std::vector<ParsedChord> parse_chord_string(const std::string& input, int startOctave,
                                                    const std::string& defaultDict,
                                                    const std::string& figurePrefix = "") {
    std::vector<ParsedChord> result;
    std::istringstream iss(input);
    std::string token;
    int octave = startOctave;
    int tokenNum = 0;

    while (iss >> token) {
        ++tokenNum;

        // Octave shifts
        if (token == "O+" || token == "O-") {
            octave += (token[1] == '+') ? 1 : -1;
            continue;
        }

        // Split on '_'
        std::vector<std::string> parts;
        {
            std::istringstream ts(token);
            std::string part;
            while (std::getline(ts, part, '_')) parts.push_back(part);
        }

        if (parts.empty()) continue;

        std::string chordName = parts[0];
        std::string dictName;
        std::string durStr;

        if (parts.size() == 3) {
            dictName = map_chord_group(parts[1]);
            durStr = parts[2];
        } else if (parts.size() == 2) {
            dictName = defaultDict;
            durStr = parts[1];
        } else {
            throw std::runtime_error("Chord token missing duration (use Root_dur or Root_group_dur): " + token + " (token #" + std::to_string(tokenNum) + ")");
        }

        if (parts.size() > 3)
            throw std::runtime_error("Too many '_' segments in chord token: " + token + " (token #" + std::to_string(tokenNum) + ")");

        // Strip trailing 'O' from chord name
        while (!chordName.empty() && chordName.back() == 'O')
            chordName.pop_back();

        if (chordName.empty())
            throw std::runtime_error("Empty chord name in token: " + token + " (token #" + std::to_string(tokenNum) + ")");

        // Validate first char is a note letter A-G
        char first = chordName[0];
        if (first < 'A' || first > 'G')
            throw std::runtime_error("Chord root must start with A-G, got: " + token + " (token #" + std::to_string(tokenNum) + ")");

        // Parse root pitch (1 or 2 chars if second is #/b)
        std::string rootName, chordType;
        if (chordName.size() >= 2 && (chordName[1] == '#' || chordName[1] == 'b')) {
            rootName = chordName.substr(0, 2);
            chordType = chordName.substr(2);
        } else {
            rootName = chordName.substr(0, 1);
            chordType = chordName.substr(1);
        }

        // Empty chord type = Major
        if (chordType.empty()) chordType = "M";

        // Validate duration string
        if (durStr.empty())
            throw std::runtime_error("Empty duration in chord token: " + token + " (token #" + std::to_string(tokenNum) + ")");

        float dur = parse_duration(durStr); // throws on invalid duration char

        Chord chord = Chord::create(dictName, rootName, octave, chordType, dur);

        // Set figure name hint if prefix specified
        if (!figurePrefix.empty()) {
            char durBuf[16];
            snprintf(durBuf, sizeof(durBuf), "%g", dur);
            chord.figureName = figurePrefix + durBuf;
        }

        result.push_back({std::move(chord), octave});
    }

    return result;
}

// ---------------------------------------------------------------------------
// Parse note input: accepts MIDI number ("60") or note name ("C4", "D#3", "Eb5")
// Returns MIDI note number as float.
// Chromatic mapping: C=0, C#/Db=1, D=2, D#/Eb=3, E=4, F=5,
//                    F#/Gb=6, G=7, G#/Ab=8, A=9, A#/Bb=10, B=11
// ---------------------------------------------------------------------------
inline float parse_note_input(const char* str) {
    if (!str || !str[0]) return 60.0f; // default C4

    // If starts with digit, treat as MIDI number
    if (std::isdigit(static_cast<unsigned char>(str[0]))) {
        return std::stof(str);
    }

    // Parse note letter
    static const int noteMap[] = {
        9,  // A
        11, // B
        0,  // C
        2,  // D
        4,  // E
        5,  // F
        7,  // G
    };

    char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(str[0])));
    if (letter < 'A' || letter > 'G')
        return 60.0f;

    int semitone = noteMap[letter - 'A'];
    int pos = 1;

    // Check for sharp/flat
    if (str[pos] == '#') {
        semitone += 1;
        pos++;
    } else if (str[pos] == 'b') {
        semitone -= 1;
        pos++;
    }

    // Wrap semitone
    semitone = ((semitone % 12) + 12) % 12;

    // Parse octave (default 4)
    int octave = 4;
    if (str[pos] && std::isdigit(static_cast<unsigned char>(str[pos]))) {
        octave = str[pos] - '0';
    }

    return float(octave * 12 + semitone);
}

// ---------------------------------------------------------------------------
// ParsedNote — result of parsing a passage string note
// ---------------------------------------------------------------------------
struct ParsedNote {
    float noteNumber;
    float durationSeconds;
};

// ---------------------------------------------------------------------------
// Parse a passage string into a sequence of notes.
// Format: 3-char commands separated by spaces.
//   Note: letter from "CdDeEFgGaAbB" (chromatic index) + duration char
//   "O+ " / "O- " for octave shifts
// Duration in seconds = beats * 60 / bpm
// ---------------------------------------------------------------------------
inline std::vector<ParsedNote> parse_passage(const char* str, int octave, float bpm) {
    // Note letter → chromatic semitone within octave
    // C=0, D=2, E=4, F=5, G=7, A=9, B=11
    static const int letterSemitone[] = {
        9,  // A
        11, // B
        0,  // C
        2,  // D
        4,  // E
        5,  // F
        7,  // G
    };

    std::vector<ParsedNote> result;
    if (!str || !str[0]) return result;

    std::istringstream iss(str);
    std::string token;
    int tokenNum = 0;

    while (iss >> token) {
        ++tokenNum;

        // Octave shifts
        if (token == "O+" || token == "O-") {
            octave += (token[1] == '+') ? 1 : -1;
            continue;
        }

        if (token.size() < 2)
            throw std::runtime_error("Passage token too short: " + token + " (token #" + std::to_string(tokenNum) + ")");

        // Parse note: letter [#/b] duration
        char letter = token[0];
        if (letter < 'A' || letter > 'G')
            throw std::runtime_error("Passage note must start with A-G, got: " + token + " (token #" + std::to_string(tokenNum) + ")");

        int semitone = letterSemitone[letter - 'A'];
        int pos = 1;

        // Optional sharp/flat
        if (pos < (int)token.size() && token[pos] == '#') {
            semitone += 1;
            pos++;
        } else if (pos < (int)token.size() && token[pos] == 'b') {
            semitone -= 1;
            pos++;
        }

        // Wrap semitone into 0-11
        semitone = ((semitone % 12) + 12) % 12;

        float noteNumber = float(octave * 12 + semitone);

        // Remaining chars: duration (required)
        if (pos >= (int)token.size())
            throw std::runtime_error("Passage note missing duration: " + token + " (token #" + std::to_string(tokenNum) + ")");

        std::string durStr = token.substr(pos);
        float beats = parse_duration(durStr); // throws on invalid duration char

        float durationSeconds = beats * 60.0f / bpm;

        result.push_back({noteNumber, durationSeconds});
    }

    return result;
}

} // namespace mforce
