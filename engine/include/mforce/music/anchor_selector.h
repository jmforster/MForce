#pragma once
// Harmonic anchor selection (comp walk round 1, spec
// docs/superpowers/specs/2026-09-21-comp-walk1-design.md §3).
//
// Given a phrase's figures (contents already realized) and the section's
// chord timeline, choose the phrase's starting pitch and its dense
// connector chain by ENUMERATING legal (start, leadSteps...) tuples —
// never sample-and-reject — under Matt's 2026-09-21 rules:
//   R1  phrase-opening downbeat is a chord tone (no member weighting)
//   R2  a phrase's last note is a chord tone
//   R3  the PASSAGE's last note is scale degree 1, period
// and soft preferences: a chord-tone note scores
// 1 + isBarFinal + isFigureFinal + isLongerThanDefaultPulse (the stacking
// "boosted thrice"), minus 0.5 per |leadStep| (cursor proximity). Choice
// among legal chains is a seeded roulette with weight exp(score), so one
// masterSeed reproduces one tune and different seeds give siblings.
//
// Intents outrank local rules: a pinned start (parallel-period literal
// repeat) bypasses the R1 candidate enumeration outright.
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/passage_melody.h"   // scale_grid_index
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mforce {

// Inverse of scale_grid_index: diatonic grid index -> note number.
inline float note_number_of_grid(int gridIdx, const Scale& scale) {
    const int len = scale.length();
    const int oct = gridIdx >= 0 ? gridIdx / len
                                 : -((-gridIdx + len - 1) / len);
    const int d = gridIdx - oct * len;
    float acc = 0.0f;
    for (int i = 0; i < d; ++i) acc += scale.ascending_step(i);
    return float(scale.offset() + 12 * oct + int(acc + 0.5f));
}

namespace detail_anchor {

struct NoteMeta {
    int fig;            // figure index
    int relGrid;        // grid offset from the FIGURE's anchor (prefix sum)
    float beat;         // absolute section beat at note start
    bool figureFinal;
    bool barFinal;
    bool longNote;
};

inline bool chord_tone(float nn, const Chord& chord) {
    const int pc = ((int(nn) % 12) + 12) % 12;
    for (const auto& p : chord.pitches) {
        if (((int(p.note_number()) % 12) + 12) % 12 == pc) return true;
    }
    return false;
}

} // namespace detail_anchor

// Selects and WRITES localTmpl.startingPitch + localTmpl.connectors
// (dense convention: [0] = disengaged optional, the dummy).
// `figures` are the phrase's realized figure contents, one per template
// figure slot, in order. Throws (naming the phrase) when no legal chain
// exists.
inline void select_anchors(PhraseTemplate& localTmpl,
                           const std::vector<const MelodicFigure*>& figures,
                           const HarmonyTimeline& timeline,
                           const Scale& scale,
                           float phraseStartBeat,
                           float beatsPerBar,
                           float defaultPulse,
                           bool isPassageFinal,
                           std::optional<Pitch> pinnedStart,
                           const Pitch& registerAnchor,
                           Randomizer& rng) {
    using detail_anchor::NoteMeta;
    const int F = int(figures.size());
    if (F == 0) return;
    const int len = scale.length();

    // ---- Rhythm-derived note metadata (chain-independent). ----
    std::vector<NoteMeta> notes;
    {
        float beat = phraseStartBeat;
        for (int f = 0; f < F; ++f) {
            int rel = 0;
            const auto& units = figures[f]->units;
            for (int j = 0; j < int(units.size()); ++j) {
                rel += units[j].step;   // step[0] is 0 by convention
                NoteMeta m;
                m.fig = f;
                m.relGrid = rel;
                m.beat = beat;
                m.figureFinal = (j == int(units.size()) - 1);
                m.barFinal = false;     // filled below
                m.longNote = units[j].duration > defaultPulse;
                notes.push_back(m);
                beat += units[j].duration;
            }
        }
        // barFinal = last note STARTING within its bar.
        for (int i = 0; i < int(notes.size()); ++i) {
            const int bar = int(notes[i].beat / beatsPerBar);
            const bool lastInBar =
                (i + 1 == int(notes.size()))
                || int(notes[i + 1].beat / beatsPerBar) != bar;
            notes[i].barFinal = lastInBar;
        }
    }
    if (notes.empty()) return;

    // ---- Resolved-chord cache (chord_at returns ScaleChord*). ----
    std::map<const ScaleChord*, Chord> resolved;
    auto chord_for_beat = [&](float beat) -> const Chord* {
        const ScaleChord* sc = timeline.chord_at(beat);
        if (!sc) return nullptr;
        auto it = resolved.find(sc);
        if (it == resolved.end()) {
            it = resolved.emplace(sc, sc->resolve(scale, 4)).first;
        }
        return &it->second;
    };

    // ---- A0 candidates. ----
    std::vector<int> a0Candidates;
    if (pinnedStart) {
        // Intent outranks R1: the literal-repeat opening is one pinned
        // decision (brainstorm 2026-09-21).
        a0Candidates.push_back(scale_grid_index(
            pinnedStart->note_number(), scale));
    } else {
        const Chord* c0 = chord_for_beat(phraseStartBeat);
        if (!c0)
            throw std::runtime_error(
                "select_anchors: phrase '" + localTmpl.name +
                "' has no chord at beat " + std::to_string(phraseStartBeat));
        const int center = scale_grid_index(registerAnchor.note_number(), scale);
        for (int g = center - 4; g <= center + 4; ++g) {
            if (detail_anchor::chord_tone(note_number_of_grid(g, scale), *c0))
                a0Candidates.push_back(g);      // R1 by construction
        }
    }
    if (a0Candidates.empty())
        throw std::runtime_error(
            "select_anchors: phrase '" + localTmpl.name +
            "' — no chord-tone start candidate satisfies R1 in the register "
            "window");

    // ---- Per-figure grid anchor from (A0, leads): figure f's anchor =
    //      previous figure's END + lead_f. Precompute figure net steps. ----
    std::vector<int> figNet(F, 0);
    for (int f = 0; f < F; ++f) figNet[f] = figures[f]->net_step();

    struct Chain {
        int a0;
        std::vector<int> leads;   // size F-1
        double score;
    };
    std::vector<Chain> legal;

    // Enumerate lead combinations via mixed-radix counting (no recursion,
    // no shell loops harmed): leads in [-4, +4].
    const int kLo = -4, kHi = 4, kRange = kHi - kLo + 1;
    long long combos = 1;
    for (int f = 1; f < F; ++f) combos *= kRange;

    for (int a0 : a0Candidates) {
        for (long long ci = 0; ci < combos; ++ci) {
            Chain ch;
            ch.a0 = a0;
            long long rem = ci;
            for (int f = 1; f < F; ++f) {
                ch.leads.push_back(kLo + int(rem % kRange));
                rem /= kRange;
            }

            // Figure anchors on the grid.
            std::vector<int> figAnchor(F);
            figAnchor[0] = a0;
            for (int f = 1; f < F; ++f) {
                figAnchor[f] = figAnchor[f - 1] + figNet[f - 1]
                             + ch.leads[f - 1];
            }

            // Walk notes: hard rules + score.
            double score = 0.0;
            bool ok = true;
            int lastGrid = 0;
            for (const auto& m : notes) {
                const int grid = figAnchor[m.fig] + m.relGrid;
                lastGrid = grid;
                const float nn = note_number_of_grid(grid, scale);
                const Chord* c = chord_for_beat(m.beat);
                const bool ct = c && detail_anchor::chord_tone(nn, *c);
                if (ct) {
                    score += 1.0 + (m.barFinal ? 1.0 : 0.0)
                                 + (m.figureFinal ? 1.0 : 0.0)
                                 + (m.longNote ? 1.0 : 0.0);
                }
            }
            // R2: last note of the phrase is a chord tone.
            {
                const auto& m = notes.back();
                const int grid = figAnchor[m.fig] + m.relGrid;
                const float nn = note_number_of_grid(grid, scale);
                const Chord* c = chord_for_beat(m.beat);
                if (!(c && detail_anchor::chord_tone(nn, *c))) ok = false;
            }
            // R3: last note of the passage is scale degree 1 (any octave).
            if (ok && isPassageFinal) {
                const int deg = ((lastGrid % len) + len) % len;
                if (deg != 0) ok = false;
            }
            if (!ok) continue;

            for (int l : ch.leads) score -= 0.5 * std::abs(l);
            ch.score = score;
            legal.push_back(std::move(ch));
        }
    }

    if (legal.empty())
        throw std::runtime_error(
            "select_anchors: phrase '" + localTmpl.name +
            "' — no anchor chain satisfies R2" +
            std::string(isPassageFinal ? "/R3" : "") +
            " over the enumerated window");

    // ---- Seeded roulette, weight = exp(score - max). ----
    double maxScore = legal.front().score;
    for (const auto& ch : legal) maxScore = std::max(maxScore, ch.score);
    double total = 0.0;
    std::vector<double> w(legal.size());
    for (size_t i = 0; i < legal.size(); ++i) {
        w[i] = std::exp(legal[i].score - maxScore);
        total += w[i];
    }
    double draw = double(rng.value()) * total;
    size_t pick = 0;
    for (; pick + 1 < legal.size(); ++pick) {
        draw -= w[pick];
        if (draw <= 0.0) break;
    }
    const Chain& chosen = legal[pick];

    // ---- Write the decision into the working template. ----
    localTmpl.startingPitch =
        Pitch::from_note_number(note_number_of_grid(chosen.a0, scale));
    localTmpl.connectors.clear();
    localTmpl.connectors.push_back(std::nullopt);   // dense dummy [0]
    for (int l : chosen.leads) {
        FigureConnector fc;
        fc.leadStep = l;
        localTmpl.connectors.push_back(fc);
    }
}

} // namespace mforce
