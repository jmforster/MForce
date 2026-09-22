#pragma once
// Passage-string -> template melody derivation (comp crawl, spec
// docs/superpowers/specs/2026-09-21-comp-crawl-mary-design.md §4).
// A comp-purpose passage string ('|' = structural phrase boundary) becomes
// PhraseTemplates of Locked figures: step[0]=0 inside a figure, the bridge
// between figures rides FigureConnector.leadStep.
#include "mforce/music/basics.h"
#include "mforce/music/parse_util.h"
#include "mforce/music/templates.h"
#include <cmath>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mforce {

// Diatonic grid index of a note number: octave * scale-length + degree,
// measured from the scale's tonic pitch class. Equal note numbers always
// map to equal indices, so deltas are scale-step distances. Throws on a
// non-scale tone — crawl material is diatonic by construction, and a
// silent snap would hide a transcription error.
inline int scale_grid_index(float nn, const Scale& scale) {
    const int len = scale.length();
    const int total = int(nn) - scale.offset();
    const int oct = (total >= 0) ? total / 12 : -((-total + 11) / 12);
    const int rem = total - 12 * oct;   // 0..11 semitones above a tonic
    float acc = 0.0f;
    for (int d = 0; d < len; ++d) {
        if (int(acc + 0.5f) == rem) return oct * len + d;
        acc += scale.ascending_step(d);
    }
    throw std::runtime_error(
        "passage melody: note number " + std::to_string(int(nn)) +
        " is not a tone of the scale");
}

inline int scale_steps_between(float nnFrom, float nnTo, const Scale& scale) {
    return scale_grid_index(nnTo, scale) - scale_grid_index(nnFrom, scale);
}

// parse_passage sets phraseStart=true on EVERY note of a '|'-free string
// (grouping only activates when a bar appears), so the derivation must
// know whether '|' grouping was active at all.
inline bool passage_has_bars_(const std::string& s) {
    return s.find('|') != std::string::npos;
}

// Derive fully-specified PhraseTemplates from a comp-purpose passage string.
// anchorOctave: house octave applied to octave-less note names (E -> E5).
// Figure boundary = barline (a note STARTING on a bar multiple opens a new
// figure); '|' = structural phrase boundary. No cadence fields are set —
// fully-specified means there is nothing for cadence machinery to decide.
//
// Connectors are DENSE (one per figure, connectors[i] = the bridge INTO
// figure i, [0] a zero dummy) — that is what composer.h:1471 and
// realize_phrase_to_events_ read. The figures themselves are never mutated:
// step[0] is 0 inside every figure, the bridge rides leadStep.
inline std::vector<PhraseTemplate> phrases_from_passage(
        const std::string& passageStr, int anchorOctave,
        const Scale& scale, float beatsPerBar) {
    // bpm 60 => ParsedNote.durationSeconds is BEATS.
    auto notes = parse_passage(passageStr.c_str(), anchorOctave, 60.0f);
    if (notes.empty())
        throw std::runtime_error("passage melody: empty passage string");

    const bool hasBars = passage_has_bars_(passageStr);

    std::vector<PhraseTemplate> phrases;
    PhraseTemplate cur;
    MelodicFigure fig;
    float passageBeat = 0.0f;   // absolute beat cursor (barlines are global)
    int prevGrid = 0;           // grid index of the previous sounded note
    int figLead = 0;            // bridge INTO the figure being accumulated
    bool inPhrase = false;
    bool firstNote = true;

    auto flush_figure = [&]() {
        if (fig.units.empty()) return;
        FigureTemplate ft;
        ft.source = FigureSource::Locked;
        ft.lockedFigure = fig;
        cur.figures.push_back(std::move(ft));
        FigureConnector fc;
        fc.leadStep = figLead;
        cur.connectors.push_back(fc);
        fig = MelodicFigure{};
    };
    auto flush_phrase = [&]() {
        flush_figure();
        if (!cur.figures.empty()) {
            cur.name = "phrase" + std::to_string(phrases.size() + 1);
            phrases.push_back(std::move(cur));
        }
        cur = PhraseTemplate{};
    };

    for (const auto& n : notes) {
        if (n.noteNumber == kRestNote)
            throw std::runtime_error(
                "passage melody: rests are not supported by the crawl "
                "derivation (parse's rest-ends-phrase rule would corrupt "
                "structural phrases)");
        const int grid = scale_grid_index(n.noteNumber, scale);
        if (inPhrase && n.phraseStart && !firstNote && hasBars)
            flush_phrase();

        const bool phraseStartNow = cur.figures.empty() && fig.units.empty();
        const float beatInBar = std::fmod(passageBeat, beatsPerBar);
        if (phraseStartNow) {
            figLead = 0;            // startingPitch anchors the phrase
        } else if (beatInBar == 0.0f && !fig.units.empty()) {
            // Barline: close the figure, bridge into the next one.
            const int lead = grid - prevGrid;
            flush_figure();
            figLead = lead;
        }

        FigureUnit u;
        u.duration = n.durationSeconds;    // == beats (bpm 60)
        u.step = fig.units.empty() ? 0 : (grid - prevGrid);
        fig.units.push_back(u);

        if (phraseStartNow)
            cur.startingPitch = Pitch::from_note_number(n.noteNumber);
        prevGrid = grid;
        passageBeat += n.durationSeconds;
        inPhrase = true;
        firstNote = false;
    }
    flush_phrase();
    return phrases;
}

} // namespace mforce
