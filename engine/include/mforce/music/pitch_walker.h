#pragma once
#include "mforce/music/basics.h"
#include <vector>
#include <algorithm>
#include <cmath>

namespace mforce {

// ---------------------------------------------------------------------------
// Pitch-walking primitives shared between Conductor (perform-time tree-walk,
// scheduled for retirement) and Composer (compose-time realize step).
// ---------------------------------------------------------------------------

// Snap a note number to the nearest pitch in the scale.
inline float snap_to_scale(float noteNumber, const Scale& scale) {
    float rel = noteNumber - float(scale.offset());
    float octaves = std::floor(rel / 12.0f);
    float pos = rel - octaves * 12.0f;
    if (pos < 0) { pos += 12.0f; octaves -= 1.0f; }

    float bestPitch = 0;
    float bestDist = 999.0f;
    float accum = 0;
    for (int d = 0; d < scale.length(); ++d) {
        float dist = std::abs(accum - pos);
        if (dist < bestDist) {
            bestDist = dist;
            bestPitch = float(scale.offset()) + octaves * 12.0f + accum;
        }
        accum += scale.ascending_step(d);
    }
    float dist = std::abs(accum - pos);
    if (dist < bestDist) {
        bestPitch = float(scale.offset()) + octaves * 12.0f + accum;
    }
    return bestPitch;
}

// Step `noteNumber` by `steps` scale degrees within `scale`.
inline float step_note(float noteNumber, int steps, const Scale& scale) {
    if (steps == 0) return noteNumber;

    float nn = noteNumber;

    if (steps > 0) {
        for (int i = 0; i < steps; ++i) {
            float rel = nn - float(scale.offset());
            while (rel < 0) rel += 12.0f;
            float pos = std::fmod(rel, 12.0f);
            float accum = 0;
            int deg = 0;
            for (int d = 0; d < scale.length(); ++d) {
                if (std::abs(accum - pos) < 0.5f) { deg = d; break; }
                accum += scale.ascending_step(d);
            }
            nn += scale.ascending_step(deg % scale.length());
        }
    } else {
        for (int i = 0; i < -steps; ++i) {
            float rel = nn - float(scale.offset());
            while (rel < 0) rel += 12.0f;
            float pos = std::fmod(rel, 12.0f);
            float accum = 0;
            int deg = 0;
            for (int d = 0; d < scale.length(); ++d) {
                if (std::abs(accum - pos) < 0.5f) { deg = d; break; }
                accum += scale.ascending_step(d);
            }
            int prevDeg = (deg - 1 + scale.length()) % scale.length();
            nn -= scale.ascending_step(prevDeg);
        }
    }
    return nn;
}

// Step `noteNumber` to nearest chord-tone, then `steps` chord-tones within
// the chord's pitch list (pre-resolved across ±2 octaves).
inline float step_chord_tone(float noteNumber, int steps, const Chord& chord) {
    if (steps == 0 || chord.pitches.empty()) return noteNumber;

    std::vector<float> tones;
    for (int octShift = -2; octShift <= 2; ++octShift) {
        for (const auto& p : chord.pitches) {
            tones.push_back(p.note_number() + 12.0f * octShift);
        }
    }
    std::sort(tones.begin(), tones.end());

    int closest = 0;
    float minDist = 999.0f;
    for (int i = 0; i < int(tones.size()); ++i) {
        float d = std::abs(tones[i] - noteNumber);
        if (d < minDist) { minDist = d; closest = i; }
    }

    if (steps > 0 && tones[closest] < noteNumber - 0.1f) {
        for (int i = closest + 1; i < int(tones.size()); ++i) {
            if (tones[i] >= noteNumber - 0.1f) { closest = i; break; }
        }
    } else if (steps < 0 && tones[closest] > noteNumber + 0.1f) {
        for (int i = closest - 1; i >= 0; --i) {
            if (tones[i] <= noteNumber + 0.1f) { closest = i; break; }
        }
    }

    int target = closest + steps;
    target = std::max(0, std::min(target, int(tones.size()) - 1));
    return tones[target];
}

} // namespace mforce
