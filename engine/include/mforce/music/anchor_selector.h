#pragma once
// Harmonic anchor selection (comp walk, specs
// docs/superpowers/specs/2026-09-21-comp-walk1-design.md §3 and
// docs/superpowers/specs/2026-09-21-comp-walk2-design.md §3).
//
// Given a phrase's figures (contents already realized) and the section's
// chord timeline, choose the phrase's starting pitch and its dense
// connector chain by ENUMERATING legal (start, leadSteps...) tuples —
// never sample-and-reject — under Matt's hard rules:
//   R1  phrase-opening downbeat is a chord tone (no member weighting)
//   R2  a phrase's last note is a chord tone
//   R3  the PASSAGE's last note is scale degree 1, period
//   R4  (walk2) the passage's last note is a PITCH THE MELODY HAS ALREADY
//       VISITED — cadential register memory; leaps stay free elsewhere
// and the walk2 sequential preferences from Matt's walk1 annotations:
// chord-tone base score with stacked bar-final/figure-final/long boosts;
// seventh-of-V resolves down (96:4); suspensions must resolve down by
// step; long notes police extensions hard; repetition across a harmony
// change caps; penultimate != final; gap-fill bonus (post-leap stepwise
// reversal); leap proximity cost scaled by position (cheap early, dear
// late); final-note regression toward the melody's mean.
//
// Choice among legal chains is a seeded roulette with weight exp(score).
// Intents outrank local rules: a pinned start (parallel-period literal
// repeat) bypasses the R1 candidate enumeration outright.
//
// Cause and effect: set env MFORCE_ANCHOR_LOG to any value and each
// phrase prints its chosen chain with a per-term score breakdown plus the
// top runners-up and per-rule elimination counts, to stderr.
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/passage_melody.h"   // scale_grid_index
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iostream>
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
    float duration;     // beats
    bool figureFinal;
    bool barFinal;
    bool longNote;
};

inline int pc_of(float nn) { return ((int(nn) % 12) + 12) % 12; }

inline bool chord_tone(float nn, const Chord& chord) {
    const int pc = pc_of(nn);
    for (const auto& p : chord.pitches) {
        if (pc_of(p.note_number()) == pc) return true;
    }
    return false;
}

// Per-term score breakdown, kept per legal chain for the decision log.
struct Terms {
    double ct{0}, seventh{0}, susp{0}, longNct{0}, rep{0},
           penult{0}, gap{0}, leap{0}, regress{0};
    double total() const {
        return ct + seventh + susp + longNct + rep + penult + gap + leap
             + regress;
    }
};

} // namespace detail_anchor

// Selects and WRITES localTmpl.startingPitch + localTmpl.connectors
// (dense convention: [0] = disengaged optional, the dummy).
// `figures` are the phrase's realized figure contents, one per template
// figure slot, in order. `visitedGrids` (optional, in/out): grid indices
// of every melody note composed so far in this passage — read for the R4
// cadential register-memory rule and the mean-regression term, and
// APPENDED with the chosen chain's notes on return. `passageTotalBeats`
// scales the leap-timing cost (0 = flat). Throws (naming the phrase)
// when no legal chain exists.
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
                           Randomizer& rng,
                           std::vector<int>* visitedGrids = nullptr,
                           float passageTotalBeats = 0.0f) {
    using detail_anchor::NoteMeta;
    using detail_anchor::Terms;
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
                m.duration = units[j].duration;
                m.figureFinal = (j == int(units.size()) - 1);
                m.barFinal = false;     // filled below
                m.longNote = units[j].duration > defaultPulse;
                notes.push_back(m);
                beat += units[j].duration;
            }
        }
        for (int i = 0; i < int(notes.size()); ++i) {
            const int bar = int(notes[i].beat / beatsPerBar);
            const bool lastInBar =
                (i + 1 == int(notes.size()))
                || int(notes[i + 1].beat / beatsPerBar) != bar;
            notes[i].barFinal = lastInBar;
        }
    }
    if (notes.empty()) return;
    const int N = int(notes.size());

    // ---- Chord lookups: raw ScaleChord* for change detection, resolved
    //      Chord for pitch-class membership. ----
    std::map<const ScaleChord*, Chord> resolved;
    auto raw_chord = [&](float beat) { return timeline.chord_at(beat); };
    auto chord_for_beat = [&](float beat) -> const Chord* {
        const ScaleChord* sc = timeline.chord_at(beat);
        if (!sc) return nullptr;
        auto it = resolved.find(sc);
        if (it == resolved.end()) {
            it = resolved.emplace(sc, sc->resolve(scale, 4)).first;
        }
        return &it->second;
    };
    // Pitch class of a chord's 7th (4th stacked pitch), or -1.
    auto seventh_pc = [&](const Chord* c) -> int {
        if (!c || int(c->pitches.size()) < 4) return -1;
        return detail_anchor::pc_of(c->pitches[3].note_number());
    };

    // ---- A0 candidates. ----
    std::vector<int> a0Candidates;
    if (pinnedStart) {
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

    std::vector<int> figNet(F, 0);
    for (int f = 0; f < F; ++f) figNet[f] = figures[f]->net_step();

    // Prior-visited stats for R4 + regression.
    const bool haveVisited = visitedGrids && !visitedGrids->empty();
    double visitedSum = 0.0;
    if (haveVisited)
        for (int g : *visitedGrids) visitedSum += g;

    struct Chain {
        int a0;
        std::vector<int> leads;   // size F-1
        Terms terms;
        double score;
    };
    std::vector<Chain> legal;
    long long r2Fail = 0, r3Fail = 0, r4Fail = 0;

    const int kLo = -4, kHi = 4, kRange = kHi - kLo + 1;
    long long combos = 1;
    for (int f = 1; f < F; ++f) combos *= kRange;

    std::vector<int> grids(N);
    std::vector<int> figAnchor(F);

    for (int a0 : a0Candidates) {
        for (long long ci = 0; ci < combos; ++ci) {
            Chain ch;
            ch.a0 = a0;
            long long rem = ci;
            for (int f = 1; f < F; ++f) {
                ch.leads.push_back(kLo + int(rem % kRange));
                rem /= kRange;
            }
            figAnchor[0] = a0;
            for (int f = 1; f < F; ++f)
                figAnchor[f] = figAnchor[f - 1] + figNet[f - 1]
                             + ch.leads[f - 1];
            for (int i = 0; i < N; ++i)
                grids[i] = figAnchor[notes[i].fig] + notes[i].relGrid;

            // ---- Hard rules. ----
            {   // R2
                const float nn = note_number_of_grid(grids[N - 1], scale);
                const Chord* c = chord_for_beat(notes[N - 1].beat);
                if (!(c && detail_anchor::chord_tone(nn, *c))) {
                    ++r2Fail;
                    continue;
                }
            }
            if (isPassageFinal) {
                const int deg = ((grids[N - 1] % len) + len) % len;
                if (deg != 0) { ++r3Fail; continue; }        // R3
                if (haveVisited || N > 1) {                  // R4
                    bool seen = false;
                    if (visitedGrids)
                        for (int g : *visitedGrids)
                            if (g == grids[N - 1]) { seen = true; break; }
                    for (int i = 0; !seen && i < N - 1; ++i)
                        if (grids[i] == grids[N - 1]) seen = true;
                    if (!seen) { ++r4Fail; continue; }
                }
            }

            // ---- Soft terms. ----
            Terms t;
            for (int i = 0; i < N; ++i) {
                const auto& m = notes[i];
                const float nn = note_number_of_grid(grids[i], scale);
                const Chord* c = chord_for_beat(m.beat);
                const bool ct = c && detail_anchor::chord_tone(nn, *c);
                if (ct) {
                    t.ct += 1.0 + (m.barFinal ? 1.0 : 0.0)
                                + (m.figureFinal ? 1.0 : 0.0)
                                + (m.longNote ? 1.0 : 0.0);
                }
                // Long notes police extensions hard.
                if (!ct && m.duration >= 2.0f) t.longNct -= 4.0;
                // Seventh of the active chord resolves down (96:4).
                if (c && i + 1 < N
                    && detail_anchor::pc_of(nn) == seventh_pc(c)) {
                    if (grids[i + 1] == grids[i] - 1)     t.seventh += 3.0;
                    else if (grids[i + 1] > grids[i])     t.seventh -= 3.0;
                }
                // Suspension: NCT of the chord this note RINGS INTO must
                // resolve down by step.
                const ScaleChord* rawStart = raw_chord(m.beat);
                const ScaleChord* rawEnd =
                    raw_chord(m.beat + m.duration - 1e-3f);
                if (rawEnd && rawEnd != rawStart) {
                    const Chord* cEnd =
                        chord_for_beat(m.beat + m.duration - 1e-3f);
                    if (cEnd && !detail_anchor::chord_tone(nn, *cEnd)) {
                        if (i + 1 < N && grids[i + 1] == grids[i] - 1)
                            t.susp += 2.0;
                        else
                            t.susp -= 4.0;
                    }
                }
            }
            // Repetition across a harmony change (> 4 beats of one pitch).
            {
                int runStart = 0;
                for (int i = 1; i <= N; ++i) {
                    if (i == N || grids[i] != grids[runStart]) {
                        float runDur = 0.0f;
                        for (int k = runStart; k < i; ++k)
                            runDur += notes[k].duration;
                        if (runDur > 4.0f
                            && raw_chord(notes[runStart].beat)
                               != raw_chord(notes[i - 1].beat
                                            + notes[i - 1].duration - 1e-3f))
                            t.rep -= 3.0;
                        runStart = i;
                    }
                }
            }
            // Penultimate != final (passage end only, light).
            if (isPassageFinal && N >= 2 && grids[N - 1] == grids[N - 2])
                t.penult -= 1.5;
            // Gap-fill: leap then stepwise reversal.
            for (int i = 1; i + 1 < N; ++i) {
                const int d1 = grids[i] - grids[i - 1];
                const int d2 = grids[i + 1] - grids[i];
                if (std::abs(d1) >= 3 && d1 * d2 < 0 && std::abs(d2) <= 2)
                    t.gap += 1.5;
            }
            // Leap timing: cheap early, dear late.
            for (int f = 1; f < F; ++f) {
                const float b = notes[0].beat;   // fallback
                float figBeat = b;
                for (int i = 0; i < N; ++i)
                    if (notes[i].fig == f) { figBeat = notes[i].beat; break; }
                double factor = 0.5;
                if (passageTotalBeats > 0.0f) {
                    if (figBeat < 0.5f * passageTotalBeats)       factor = 0.25;
                    else if (figBeat >= 0.75f * passageTotalBeats) factor = 1.0;
                }
                t.leap -= factor * std::abs(ch.leads[f - 1]);
            }
            // Final-note regression toward the melody's mean (Matt: the
            // high-C ending; Huron: extremes regress).
            if (isPassageFinal) {
                double sum = visitedSum;
                long long cnt = haveVisited ? (long long)visitedGrids->size() : 0;
                for (int i = 0; i < N - 1; ++i) { sum += grids[i]; ++cnt; }
                if (cnt > 0) {
                    const double mean = sum / double(cnt);
                    const double dist = std::abs(double(grids[N - 1]) - mean);
                    t.regress -= 0.75 * std::max(0.0, dist - 2.0);
                }
            }

            ch.terms = t;
            ch.score = t.total();
            legal.push_back(std::move(ch));
        }
    }

    if (legal.empty())
        throw std::runtime_error(
            "select_anchors: phrase '" + localTmpl.name +
            "' — no anchor chain satisfies the hard rules over the "
            "enumerated window (R2 fails " + std::to_string(r2Fail) +
            ", R3 fails " + std::to_string(r3Fail) +
            ", R4 fails " + std::to_string(r4Fail) + ")");

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
    const Chain chosen = legal[pick];

    // ---- Decision log (env-gated): cause and effect on demand. ----
    if (std::getenv("MFORCE_ANCHOR_LOG")) {
        auto describe = [&](const Chain& ch) {
            std::string s = "start nn "
                + std::to_string(int(note_number_of_grid(ch.a0, scale)))
                + " leads [";
            for (size_t k = 0; k < ch.leads.size(); ++k)
                s += (k ? "," : "") + std::to_string(ch.leads[k]);
            const auto& t = ch.terms;
            char buf[256];
            std::snprintf(buf, sizeof buf,
                "] score %.2f (ct %.1f 7th %.1f susp %.1f longNCT %.1f "
                "rep %.1f penult %.1f gap %.1f leap %.2f regress %.2f)",
                ch.score, t.ct, t.seventh, t.susp, t.longNct, t.rep,
                t.penult, t.gap, t.leap, t.regress);
            return s + buf;
        };
        std::vector<size_t> order(legal.size());
        for (size_t i = 0; i < order.size(); ++i) order[i] = i;
        std::partial_sort(order.begin(),
                          order.begin() + std::min<size_t>(4, order.size()),
                          order.end(),
                          [&](size_t a, size_t b) {
                              return legal[a].score > legal[b].score;
                          });
        std::cerr << "[anchor] phrase '" << localTmpl.name << "': "
                  << legal.size() << " legal chains (eliminated R2 "
                  << r2Fail << ", R3 " << r3Fail << ", R4 " << r4Fail
                  << ")\n[anchor]   CHOSEN " << describe(chosen) << "\n";
        int shown = 0;
        for (size_t oi = 0; oi < order.size() && shown < 3; ++oi) {
            if (&legal[order[oi]] == &legal[pick]) continue;
            std::cerr << "[anchor]   alt    " << describe(legal[order[oi]])
                      << "\n";
            ++shown;
        }
    }

    // ---- Write the decision; publish visited notes. ----
    localTmpl.startingPitch =
        Pitch::from_note_number(note_number_of_grid(chosen.a0, scale));
    localTmpl.connectors.clear();
    localTmpl.connectors.push_back(std::nullopt);   // dense dummy [0]
    for (int l : chosen.leads) {
        FigureConnector fc;
        fc.leadStep = l;
        localTmpl.connectors.push_back(fc);
    }
    if (visitedGrids) {
        figAnchor[0] = chosen.a0;
        for (int f = 1; f < F; ++f)
            figAnchor[f] = figAnchor[f - 1] + figNet[f - 1]
                         + chosen.leads[f - 1];
        for (int i = 0; i < N; ++i)
            visitedGrids->push_back(figAnchor[notes[i].fig]
                                    + notes[i].relGrid);
    }
}

} // namespace mforce
