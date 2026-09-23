#pragma once
// Harmonic anchor selection (comp walk). Specs:
//   docs/superpowers/specs/2026-09-21-comp-walk1-design.md §3
//   docs/superpowers/specs/2026-09-21-comp-walk2-design.md §3
//   docs/superpowers/specs/2026-09-22-comp-walk3-design.md §1-3
//
// Given a phrase's figures (contents already realized) and the section's
// chord timeline, choose the phrase's starting pitch and its dense
// connector chain by ENUMERATING legal (start, leadSteps...) tuples —
// never sample-and-reject — under Matt's hard rules:
//   R1  phrase-opening downbeat is a chord tone (no member weighting)
//   R2  a phrase's last note is a chord tone
//   R3  the PASSAGE's last note is scale degree 1, period
//   R4  the passage's last note is a PITCH THE MELODY HAS ALREADY VISITED
//
// Every legal chain is scored from the genre profile (walk3): note map
// (tendency odds + NCT licenses, note_map.h) + chord-tone placement +
// phrase critic (phrase_critic.h). Choice is a seeded roulette with weight
// exp(score), restricted to chains within the departure budget when any
// exist. A pinned start (parallel-period literal repeat) bypasses the R1
// candidate enumeration outright.
//
// Cause and effect: with wantLog the result carries the chosen chain and
// top runners-up with a per-category breakdown plus every named departure.
// select_anchors prints nothing itself; the caller emits the log.
#include "mforce/music/basics.h"
#include "mforce/music/figures.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/melody_profile.h"
#include "mforce/music/note_map.h"         // note_number_of_grid, TrackNote
#include "mforce/music/phrase_critic.h"
#include "mforce/music/passage_melody.h"   // scale_grid_index
#include "mforce/music/templates.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace mforce {

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

// Per-category score breakdown, kept per legal chain for the decision log.
struct Breakdown {
    double map{0}, place{0};
    CriticTerms critic;
    double total() const { return map + place + critic.total(); }
};

} // namespace detail_anchor

struct AnchorResult {
    double score{0};
    int departures{0};
    bool inBudget{true};
    std::vector<TrackNote> notes;   // the chosen chain's notes, for the prior track
    std::string log;                // filled only when wantLog
};

// Selects and WRITES localTmpl.startingPitch + localTmpl.connectors
// (dense convention: [0] = disengaged optional, the dummy).
// `figures` are the phrase's realized figure contents, one per template
// figure slot, in order. `priorTrack` = every melody note composed so far in
// this passage (read for R4, the note map's approach context and the
// regression mean); the caller appends result.notes to it. `passageTotalBeats`
// scales the leap-timing cost (0 = flat). Throws (naming the phrase) when no
// legal chain exists.
inline AnchorResult select_anchors(PhraseTemplate& localTmpl,
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
                                   const MelodyProfile& profile,
                                   const std::vector<TrackNote>& priorTrack,
                                   float passageTotalBeats,
                                   bool wantLog) {
    using detail_anchor::NoteMeta;
    using detail_anchor::Breakdown;
    AnchorResult result;
    const int F = int(figures.size());
    if (F == 0) return result;
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
    if (notes.empty()) return result;
    const int N = int(notes.size());

    ChordLookup chords(timeline, scale);

    // ---- A0 candidates. ----
    std::vector<int> a0Candidates;
    if (pinnedStart) {
        a0Candidates.push_back(scale_grid_index(
            pinnedStart->note_number(), scale));
    } else {
        const Chord* c0 = chords.resolved(phraseStartBeat);
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

    const bool haveVisited = !priorTrack.empty();
    const int P = int(priorTrack.size());

    // Chain-independent placement weight per note (applied iff chord tone).
    std::vector<double> placeW(N);
    for (int i = 0; i < N; ++i) {
        const auto& m = notes[i];
        placeW[i] = profile.placement.chordTone
                  + (m.barFinal ? profile.placement.barFinal : 0.0)
                  + (m.figureFinal ? profile.placement.figureFinal : 0.0)
                  + (m.longNote ? profile.placement.longNote : 0.0);
    }

    struct Chain {
        int a0;
        std::vector<int> leads;   // size F-1
        Breakdown terms;
        double score;
        int departures;
        bool inBudget;
    };
    std::vector<Chain> legal;
    long long r2Fail = 0, r3Fail = 0, r4Fail = 0;

    const int kLo = -4, kHi = 4, kRange = kHi - kLo + 1;
    long long combos = 1;
    for (int f = 1; f < F; ++f) combos *= kRange;

    std::vector<int> grids(N);
    std::vector<int> figAnchor(F);
    // Reused per chain: prior track + chain notes, and the phrase view.
    std::vector<TrackNote> track(priorTrack);
    track.resize(size_t(P + N));
    std::vector<PhraseNote> ph(N);

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
                const Chord* c = chords.resolved(notes[N - 1].beat);
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
                    for (const auto& pn : priorTrack)
                        if (pn.grid == grids[N - 1]) { seen = true; break; }
                    for (int i = 0; !seen && i < N - 1; ++i)
                        if (grids[i] == grids[N - 1]) seen = true;
                    if (!seen) { ++r4Fail; continue; }
                }
            }

            // ---- Profile scoring. ----
            for (int i = 0; i < N; ++i) {
                track[size_t(P + i)] = {grids[i], notes[i].beat, notes[i].duration};
                ph[size_t(i)] = {grids[i], notes[i].beat, notes[i].duration, notes[i].fig};
            }
            Breakdown t;
            const NoteMapResult map = evaluate_note_map(
                track, P, chords, scale, beatsPerBar, profile, false);
            t.map = map.logScore;
            for (int i = 0; i < N; ++i) {
                const float nn = note_number_of_grid(grids[i], scale);
                const Chord* c = chords.resolved(notes[i].beat);
                if (c && detail_anchor::chord_tone(nn, *c)) t.place += placeW[i];
            }
            t.critic = score_phrase_critic(ph, ch.leads, priorTrack,
                                           isPassageFinal, passageTotalBeats,
                                           chords, profile.critic);
            ch.terms = t;
            ch.score = t.total();
            ch.departures = map.departures;
            ch.inBudget = map.departures <= profile.critic.departureBudget;
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

    // ---- Seeded roulette, weight = exp(score - max), restricted to
    //      in-budget chains when any exist. ----
    bool anyIn = false;
    for (const auto& ch : legal) if (ch.inBudget) { anyIn = true; break; }
    auto eligible = [&](const Chain& ch) { return !anyIn || ch.inBudget; };
    double maxScore = -1e300;
    for (const auto& ch : legal)
        if (eligible(ch)) maxScore = std::max(maxScore, ch.score);
    double total = 0.0;
    std::vector<double> w(legal.size(), 0.0);
    for (size_t i = 0; i < legal.size(); ++i) {
        if (!eligible(legal[i])) continue;
        w[i] = std::exp(legal[i].score - maxScore);
        total += w[i];
    }
    double draw = double(rng.value()) * total;
    size_t pick = legal.size();
    size_t lastEligible = 0;
    for (size_t i = 0; i < legal.size(); ++i) {
        if (!eligible(legal[i])) continue;
        lastEligible = i;
        draw -= w[i];
        if (draw <= 0.0) { pick = i; break; }
    }
    if (pick == legal.size()) pick = lastEligible;
    const Chain chosen = legal[pick];

    // ---- Chosen chain's notes. ----
    figAnchor[0] = chosen.a0;
    for (int f = 1; f < F; ++f)
        figAnchor[f] = figAnchor[f - 1] + figNet[f - 1] + chosen.leads[f - 1];
    for (int i = 0; i < N; ++i) {
        const int g = figAnchor[notes[i].fig] + notes[i].relGrid;
        result.notes.push_back({g, notes[i].beat, notes[i].duration});
    }
    result.score = chosen.score;
    result.departures = chosen.departures;
    result.inBudget = chosen.inBudget;

    // ---- Decision log (on request): cause and effect. ----
    if (wantLog) {
        auto describe = [&](const Chain& ch) {
            std::string s = "start nn "
                + std::to_string(int(note_number_of_grid(ch.a0, scale)))
                + " leads [";
            for (size_t k = 0; k < ch.leads.size(); ++k)
                s += (k ? "," : "") + std::to_string(ch.leads[k]);
            const auto& t = ch.terms;
            char buf[320];
            std::snprintf(buf, sizeof buf,
                "] score %.2f (map %.2f place %.1f rep %.1f motion %.2f "
                "range %.1f gap %.1f leap %.2f regress %.2f penult %.1f "
                "dep %d%s)",
                ch.score, t.map, t.place, t.critic.rep, t.critic.motion,
                t.critic.range, t.critic.gap, t.critic.leap,
                t.critic.regress, t.critic.penult, ch.departures,
                ch.inBudget ? "" : " OVER");
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
        std::string& L = result.log;
        L += "[anchor] phrase '" + localTmpl.name + "': "
           + std::to_string(legal.size()) + " legal chains (eliminated R2 "
           + std::to_string(r2Fail) + ", R3 " + std::to_string(r3Fail)
           + ", R4 " + std::to_string(r4Fail) + ")"
           + (anyIn ? "" : " — NO chain within the departure budget")
           + "\n[anchor]   CHOSEN " + describe(chosen) + "\n";
        int shown = 0;
        for (size_t oi = 0; oi < order.size() && shown < 3; ++oi) {
            if (order[oi] == pick) continue;
            L += "[anchor]   alt    " + describe(legal[order[oi]]) + "\n";
            ++shown;
        }
        // Named departures of the chosen chain.
        std::vector<TrackNote> chosenTrack(priorTrack);
        chosenTrack.insert(chosenTrack.end(), result.notes.begin(), result.notes.end());
        const NoteMapResult vm = evaluate_note_map(
            chosenTrack, P, chords, scale, beatsPerBar, profile, true);
        for (const auto& v : vm.verdicts) {
            if (!v.departure) continue;
            const int bar = int(std::floor(v.onset / beatsPerBar)) + 1;
            const float beatInBar = v.onset - float(bar - 1) * beatsPerBar + 1.0f;
            char buf[256];
            std::snprintf(buf, sizeof buf,
                "[departure] phrase '%s' bar %d beat %g nn %d: %s odds %.1f\n",
                localTmpl.name.c_str(), bar, double(beatInBar),
                int(note_number_of_grid(v.grid, scale)), v.kind.c_str(),
                v.odds);
            L += buf;
        }
    }

    // ---- Write the decision. ----
    localTmpl.startingPitch =
        Pitch::from_note_number(note_number_of_grid(chosen.a0, scale));
    localTmpl.connectors.clear();
    localTmpl.connectors.push_back(std::nullopt);   // dense dummy [0]
    for (int l : chosen.leads) {
        FigureConnector fc;
        fc.leadStep = l;
        localTmpl.connectors.push_back(fc);
    }
    return result;
}

} // namespace mforce
