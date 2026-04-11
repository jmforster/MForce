#pragma once
// midi_parser.h — Header-only Standard MIDI File parser (C++17)
// Self-contained: no engine dependencies. Uses fopen/fread for I/O.

#ifndef MIDI_PARSER_H
#define MIDI_PARSER_H

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace midi {

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

struct NoteEvent {
    uint8_t  pitch;           // MIDI note 0-127
    uint8_t  velocity;        // 0-127
    uint8_t  channel;         // 0-15
    double   start_beat;      // absolute position in quarter notes
    double   duration_beats;  // length in quarter notes
};

struct TempoEvent {
    uint32_t tick;
    uint32_t us_per_quarter;
};

struct TimeSignatureEvent {
    uint32_t tick;
    uint8_t  numerator;
    uint8_t  denominator_power;
};

struct Track {
    std::string           name;
    std::vector<NoteEvent> notes;
};

struct MidiParseResult {
    uint16_t                        format;    // 0 or 1
    uint16_t                        tpqn;      // ticks per quarter note
    std::vector<Track>              tracks;
    std::vector<TempoEvent>         tempos;
    std::vector<TimeSignatureEvent> time_sigs;
    std::string                     error;     // empty on success
};

struct FigureUnit {
    float duration;
    int   step;
};

// ---------------------------------------------------------------------------
// ScaleMap — maps MIDI pitches to scale degrees
// ---------------------------------------------------------------------------

struct ScaleMap {
    int              root_pitch;        // MIDI note of root (e.g. 60 = C4)
    std::vector<int> semitone_offsets;  // e.g. major = {0,2,4,5,7,9,11}

    // Return (octave_offset, degree_index) relative to root.
    // degree_index is the closest scale degree; octave_offset counts octaves
    // above/below root_pitch.
    std::pair<int,int> pitch_to_degree(int midi_pitch) const {
        if (semitone_offsets.empty()) return {0, 0};

        int rel = midi_pitch - root_pitch;
        int scale_size = static_cast<int>(semitone_offsets.size());
        constexpr int span = 12;  // chromatic span is always 12 semitones

        // Compute octave and remainder semitone within [0, 12)
        int octave = 0;
        int semi   = rel;
        if (semi >= 0) {
            octave = semi / span;
            semi   = semi % span;
        } else {
            // For negative, floor-divide
            octave = -(((-semi - 1) / span) + 1);
            semi   = semi - octave * span;
        }

        // Find closest degree
        int best_idx  = 0;
        int best_dist = 9999;
        for (int i = 0; i < scale_size; ++i) {
            int d = std::abs(semi - semitone_offsets[i]);
            if (d < best_dist) {
                best_dist = d;
                best_idx  = i;
            }
        }
        return {octave, best_idx};
    }

    // Absolute degree: octave * scale_size + degree_index.
    // A chromatic scale yields the semitone offset from root; a major scale
    // yields a compact step number useful for FigureUnit.
    int absolute_degree(int midi_pitch) const {
        auto [oct, deg] = pitch_to_degree(midi_pitch);
        int scale_size = static_cast<int>(semitone_offsets.size());
        return oct * scale_size + deg;
    }

    // Factory helpers
    static ScaleMap major(int root) {
        return ScaleMap{root, {0, 2, 4, 5, 7, 9, 11}};
    }
    static ScaleMap natural_minor(int root) {
        return ScaleMap{root, {0, 2, 3, 5, 7, 8, 10}};
    }
    static ScaleMap harmonic_minor(int root) {
        return ScaleMap{root, {0, 2, 3, 5, 7, 8, 11}};
    }
    static ScaleMap chromatic(int root) {
        return ScaleMap{root, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}};
    }
    static ScaleMap pentatonic_major(int root) {
        return ScaleMap{root, {0, 2, 4, 7, 9}};
    }
};

// ---------------------------------------------------------------------------
// Internal binary reader
// ---------------------------------------------------------------------------

namespace detail {

struct Reader {
    const uint8_t* data = nullptr;
    size_t         size = 0;
    size_t         pos  = 0;

    bool has(size_t n) const { return pos + n <= size; }

    uint8_t read_u8() {
        if (!has(1)) return 0;
        return data[pos++];
    }

    uint16_t read_be16() {
        if (!has(2)) return 0;
        uint16_t v = static_cast<uint16_t>(data[pos] << 8) | data[pos + 1];
        pos += 2;
        return v;
    }

    uint32_t read_be32() {
        if (!has(4)) return 0;
        uint32_t v = (static_cast<uint32_t>(data[pos])     << 24)
                   | (static_cast<uint32_t>(data[pos + 1]) << 16)
                   | (static_cast<uint32_t>(data[pos + 2]) << 8)
                   |  static_cast<uint32_t>(data[pos + 3]);
        pos += 4;
        return v;
    }

    uint32_t read_vlq() {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            if (!has(1)) return value;
            uint8_t byte = data[pos++];
            value = (value << 7) | (byte & 0x7F);
            if ((byte & 0x80) == 0) break;
        }
        return value;
    }

    void skip(size_t n) {
        if (pos + n > size) pos = size;
        else                pos += n;
    }

    // Return a sub-reader for a chunk of `n` bytes at current position,
    // then advance past them.
    Reader sub(size_t n) {
        Reader r;
        if (has(n)) {
            r.data = data + pos;
            r.size = n;
            r.pos  = 0;
        }
        skip(n);
        return r;
    }
};

// Pending note-on waiting for a matching note-off.
struct PendingNote {
    uint8_t  velocity;
    uint32_t on_tick;
};

// Key for pending note map: (channel, pitch).
using NoteKey = std::pair<uint8_t, uint8_t>;

// Convert an absolute tick to beats (quarter notes) given tpqn.
inline double tick_to_beat(uint32_t tick, uint16_t tpqn) {
    if (tpqn == 0) return 0.0;
    return static_cast<double>(tick) / static_cast<double>(tpqn);
}

// Parse a single MTrk chunk. Populates the supplied track, tempos, and
// time signatures. Returns an error string on failure (empty on success).
inline std::string parse_track(Reader& r, uint16_t tpqn,
                               Track& track,
                               std::vector<TempoEvent>& tempos,
                               std::vector<TimeSignatureEvent>& time_sigs) {
    uint32_t abs_tick    = 0;
    uint8_t  running     = 0;  // running status byte
    std::map<NoteKey, std::vector<PendingNote>> pending;

    while (r.has(1)) {
        uint32_t delta = r.read_vlq();
        abs_tick += delta;

        if (!r.has(1)) return "unexpected end of track data";

        uint8_t byte = r.data[r.pos];

        // ----- Meta event (0xFF) -----
        if (byte == 0xFF) {
            r.pos++;  // consume 0xFF
            if (!r.has(1)) return "unexpected end in meta event";
            uint8_t meta_type = r.read_u8();
            uint32_t len      = r.read_vlq();

            size_t start_pos = r.pos;

            if (meta_type == 0x03 && len > 0) {
                // Track name
                if (r.has(len)) {
                    track.name.assign(reinterpret_cast<const char*>(r.data + r.pos), len);
                }
            } else if (meta_type == 0x51 && len == 3) {
                // Tempo
                if (r.has(3)) {
                    uint32_t us = (static_cast<uint32_t>(r.data[r.pos])     << 16)
                                | (static_cast<uint32_t>(r.data[r.pos + 1]) << 8)
                                |  static_cast<uint32_t>(r.data[r.pos + 2]);
                    tempos.push_back({abs_tick, us});
                }
            } else if (meta_type == 0x58 && len >= 2) {
                // Time signature
                if (r.has(2)) {
                    time_sigs.push_back({abs_tick, r.data[r.pos], r.data[r.pos + 1]});
                }
            } else if (meta_type == 0x2F) {
                // End of track — close any pending notes at this tick
                for (auto& [key, vec] : pending) {
                    for (auto& pn : vec) {
                        NoteEvent ev;
                        ev.pitch          = key.second;
                        ev.velocity       = pn.velocity;
                        ev.channel        = key.first;
                        ev.start_beat     = tick_to_beat(pn.on_tick, tpqn);
                        ev.duration_beats = tick_to_beat(abs_tick - pn.on_tick, tpqn);
                        track.notes.push_back(ev);
                    }
                }
                pending.clear();
                // Advance past remaining meta bytes then return
                r.pos = start_pos + len;
                return {};
            }

            // Skip to end of meta data
            r.pos = start_pos + len;
            running = 0;  // meta events clear running status
            continue;
        }

        // ----- SysEx (0xF0 / 0xF7) -----
        if (byte == 0xF0 || byte == 0xF7) {
            r.pos++;  // consume status
            uint32_t len = r.read_vlq();
            r.skip(len);
            running = 0;
            continue;
        }

        // ----- Channel events -----
        uint8_t status;
        if (byte & 0x80) {
            // New status byte
            status = r.read_u8();
            running = status;
        } else {
            // Running status — reuse previous, don't consume this byte
            if (running == 0) {
                return "unexpected data byte with no running status";
            }
            status = running;
        }

        uint8_t high    = status & 0xF0;
        uint8_t channel = status & 0x0F;

        // Determine how many data bytes this message has.
        switch (high) {
            case 0x80: // Note Off
            case 0x90: // Note On
            case 0xA0: // Aftertouch
            case 0xB0: // Control Change
            case 0xE0: // Pitch Bend
            {
                if (!r.has(2)) return "unexpected end of channel event";
                uint8_t d1 = r.read_u8();
                uint8_t d2 = r.read_u8();

                bool is_note_off = (high == 0x80) || (high == 0x90 && d2 == 0);
                bool is_note_on  = (high == 0x90 && d2 > 0);

                if (is_note_on) {
                    NoteKey key{channel, d1};
                    pending[key].push_back({d2, abs_tick});
                } else if (is_note_off) {
                    NoteKey key{channel, d1};
                    auto it = pending.find(key);
                    if (it != pending.end() && !it->second.empty()) {
                        PendingNote pn = it->second.front();
                        it->second.erase(it->second.begin());
                        if (it->second.empty()) pending.erase(it);

                        NoteEvent ev;
                        ev.pitch          = d1;
                        ev.velocity       = pn.velocity;
                        ev.channel        = channel;
                        ev.start_beat     = tick_to_beat(pn.on_tick, tpqn);
                        ev.duration_beats = tick_to_beat(abs_tick - pn.on_tick, tpqn);
                        track.notes.push_back(ev);
                    }
                }
                break;
            }
            case 0xC0: // Program Change
            case 0xD0: // Channel Pressure
            {
                if (!r.has(1)) return "unexpected end of channel event";
                r.read_u8();  // single data byte, not needed
                break;
            }
            default:
                // Unknown status — skip
                break;
        }
    }

    // Close any remaining pending notes (no end-of-track marker reached)
    for (auto& [key, vec] : pending) {
        for (auto& pn : vec) {
            NoteEvent ev;
            ev.pitch          = key.second;
            ev.velocity       = pn.velocity;
            ev.channel        = key.first;
            ev.start_beat     = tick_to_beat(pn.on_tick, tpqn);
            ev.duration_beats = tick_to_beat(abs_tick - pn.on_tick, tpqn);
            track.notes.push_back(ev);
        }
    }

    return {};
}

} // namespace detail

// ---------------------------------------------------------------------------
// parse_file — read and parse a Standard MIDI File
// ---------------------------------------------------------------------------

inline MidiParseResult parse_file(const std::string& path) {
    MidiParseResult result{};

    // Read entire file into memory
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) {
        result.error = "cannot open file: " + path;
        return result;
    }

    std::fseek(f, 0, SEEK_END);
    long file_size = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);

    if (file_size <= 0) {
        std::fclose(f);
        result.error = "file is empty or unreadable: " + path;
        return result;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(file_size));
    size_t read_count = std::fread(buf.data(), 1, buf.size(), f);
    std::fclose(f);

    if (read_count != buf.size()) {
        result.error = "incomplete read of file: " + path;
        return result;
    }

    detail::Reader r{buf.data(), buf.size(), 0};

    // --- MThd header ---
    if (!r.has(14)) {
        result.error = "file too small for MThd header";
        return result;
    }

    uint32_t mthd_id   = r.read_be32();
    uint32_t mthd_len  = r.read_be32();

    if (mthd_id != 0x4D546864) {  // "MThd"
        result.error = "not a MIDI file (bad MThd magic)";
        return result;
    }

    if (mthd_len < 6) {
        result.error = "MThd chunk too short";
        return result;
    }

    result.format = r.read_be16();
    uint16_t num_tracks = r.read_be16();
    result.tpqn = r.read_be16();

    // Skip any extra header bytes beyond the standard 6
    if (mthd_len > 6) {
        r.skip(mthd_len - 6);
    }

    // Check for SMPTE timing (bit 15 set) — not supported
    if (result.tpqn & 0x8000) {
        result.error = "SMPTE time division is not supported";
        return result;
    }

    if (result.tpqn == 0) {
        result.error = "ticks per quarter note is zero";
        return result;
    }

    // --- Parse track chunks ---
    for (uint16_t t = 0; t < num_tracks; ++t) {
        if (!r.has(8)) {
            result.error = "unexpected end of file before track " + std::to_string(t);
            return result;
        }

        uint32_t chunk_id  = r.read_be32();
        uint32_t chunk_len = r.read_be32();

        if (chunk_id != 0x4D54726B) {  // "MTrk"
            // Unknown chunk — skip
            r.skip(chunk_len);
            continue;
        }

        detail::Reader track_reader = r.sub(chunk_len);

        Track track;
        std::string err = detail::parse_track(track_reader, result.tpqn,
                                              track, result.tempos,
                                              result.time_sigs);
        if (!err.empty()) {
            result.error = "track " + std::to_string(t) + ": " + err;
            return result;
        }

        // Sort notes by start_beat then by pitch
        std::sort(track.notes.begin(), track.notes.end(),
                  [](const NoteEvent& a, const NoteEvent& b) {
                      if (a.start_beat != b.start_beat) return a.start_beat < b.start_beat;
                      return a.pitch < b.pitch;
                  });

        result.tracks.push_back(std::move(track));
    }

    // Sort tempos and time sigs by tick
    std::sort(result.tempos.begin(), result.tempos.end(),
              [](const TempoEvent& a, const TempoEvent& b) {
                  return a.tick < b.tick;
              });
    std::sort(result.time_sigs.begin(), result.time_sigs.end(),
              [](const TimeSignatureEvent& a, const TimeSignatureEvent& b) {
                  return a.tick < b.tick;
              });

    return result;
}

// ---------------------------------------------------------------------------
// to_figure_units — convert NoteEvents to FigureUnit sequence
// ---------------------------------------------------------------------------
// Each NoteEvent becomes one FigureUnit whose duration is the note's
// duration_beats and whose step is the scale degree (absolute) of the pitch.
// Rests (gaps between consecutive notes) are represented as FigureUnits with
// step = -1 (sentinel for rest).

inline std::vector<FigureUnit> to_figure_units(const std::vector<NoteEvent>& notes,
                                                const ScaleMap& scale) {
    std::vector<FigureUnit> units;
    if (notes.empty()) return units;

    // Work with a sorted copy
    std::vector<const NoteEvent*> sorted;
    sorted.reserve(notes.size());
    for (auto& n : notes) sorted.push_back(&n);
    std::sort(sorted.begin(), sorted.end(),
              [](const NoteEvent* a, const NoteEvent* b) {
                  return a->start_beat < b->start_beat;
              });

    double cursor = sorted.front()->start_beat;
    int prev_degree = 0;
    bool first = true;

    for (const NoteEvent* note : sorted) {
        // Insert rest if there is a gap before this note
        double gap = note->start_beat - cursor;
        if (gap > 1e-6) {
            units.push_back({static_cast<float>(gap), 0}); // rest: step=0
            cursor += gap;
        }

        int deg = scale.absolute_degree(note->pitch);
        int step = first ? 0 : (deg - prev_degree);
        first = false;
        prev_degree = deg;
        units.push_back({static_cast<float>(note->duration_beats), step});
        cursor = note->start_beat + note->duration_beats;
    }

    return units;
}

} // namespace midi

#endif // MIDI_PARSER_H
