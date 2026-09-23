#pragma once
// Genre note map (walk3 spec 2026-09-22 §1). Merges a melody track into
// pitch EVENTS (repeats = one held pitch), classifies each event against the
// chord(s) it sounds over, and scores it with the profile's odds:
//   chord tones with a tendency row -> odds of the departure motion taken
//   non-chord tones -> license odds by approach/departure type
// Contribution per event = ln(odds / 100); odds < departureBelow = a
// DEPARTURE (counted for the phrase critic's budget, named in the log).
#include "mforce/music/basics.h"
#include "mforce/music/harmony_timeline.h"
#include "mforce/music/melody_profile.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace mforce {

// Inverse of scale_grid_index: diatonic grid index -> note number.
// (Moved from anchor_selector.h, unchanged.)
inline float note_number_of_grid(int gridIdx, const Scale& scale) {
    const int len = scale.length();
    const int oct = gridIdx >= 0 ? gridIdx / len
                                 : -((-gridIdx + len - 1) / len);
    const int d = gridIdx - oct * len;
    float acc = 0.0f;
    for (int i = 0; i < d; ++i) acc += scale.ascending_step(i);
    return float(scale.offset() + 12 * oct + int(acc + 0.5f));
}

struct TrackNote { int grid; float beat; float duration; };

struct PitchEvent { int grid; float onset; float end; int firstNote; int lastNote; };

inline std::vector<PitchEvent> merge_events(const std::vector<TrackNote>& t) {
    std::vector<PitchEvent> ev;
    for (int i = 0; i < int(t.size()); ++i) {
        if (!ev.empty() && ev.back().grid == t[i].grid) {
            ev.back().end = t[i].beat + t[i].duration;
            ev.back().lastNote = i;
        } else {
            ev.push_back({t[i].grid, t[i].beat, t[i].beat + t[i].duration, i, i});
        }
    }
    return ev;
}

class ChordLookup {
public:
    ChordLookup(const HarmonyTimeline& tl, const Scale& s) : tl_(tl), scale_(s) {}
    const ScaleChord* raw(float beat) const { return tl_.chord_at(beat); }
    const Chord* resolved(float beat) {
        const ScaleChord* sc = tl_.chord_at(beat);
        if (!sc) return nullptr;
        auto it = cache_.find(sc);
        if (it == cache_.end()) it = cache_.emplace(sc, sc->resolve(scale_, 4)).first;
        return &it->second;
    }
    // Chord-member number of nn's pitch class in c: 1, 3, 5, 7 (stacked
    // order of c.pitches), or 0 for a non-chord tone.
    static int member(float nn, const Chord& c) {
        const int pc = ((int(nn) % 12) + 12) % 12;
        for (int i = 0; i < int(c.pitches.size()); ++i) {
            const int cpc = ((int(c.pitches[i].note_number()) % 12) + 12) % 12;
            if (cpc == pc) return 1 + 2 * i;
        }
        return 0;
    }
private:
    const HarmonyTimeline& tl_;
    const Scale& scale_;
    std::map<const ScaleChord*, Chord> cache_;
};

struct EventVerdict {
    int event;
    float onset;
    int grid;
    std::string kind;    // "passing" ... or "tendency V7:7 step_down"
    double odds;         // per 100
    bool departure;
};

struct NoteMapResult {
    double logScore{0};
    int departures{0};
    std::vector<EventVerdict> verdicts;   // filled only when keepVerdicts
};

// Roman label of a ScaleChord for tendency-row keys ("V7", "I", "ii" ...).
// Same format as ChordLabel::to_string, but keyed on ChordDef::shortName:
// ChordLabel::to_string compares quality->name against "7"/"M7"/"m7" while
// the ChordDef table's names are "Dominant 7th" etc., so it labels G7 as
// "V". Left untouched there (chord_walker depends on it); fixed here so the
// spec's "V7:7" rows match.
inline std::string tendency_chord_label(const ScaleChord& sc) {
    static const char* romans[] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char* romansLower[] = {"i", "ii", "iii", "iv", "v", "vi", "vii"};
    std::string result;
    if (sc.alteration == -1) result += 'b';
    else if (sc.alteration == 1) result += '#';
    const std::string sn = sc.quality ? sc.quality->shortName : std::string("");
    const bool isMinor = (sn == "m" || sn == "m7");
    const int deg = ((sc.degree % 7) + 7) % 7;
    result += isMinor ? romansLower[deg] : romans[deg];
    if (sn == "7") result += "7";
    else if (sn == "M7") result += "M7";
    else if (sn == "m7") result += "m7";
    else if (sn == "dim") result += "o";
    else if (sn == "+") result += "+";
    return result;
}

inline const char* motion_name(int d) {
    if (d == 1)  return "step_up";
    if (d == -1) return "step_down";
    if (d == 2)  return "third_up";
    if (d == -2) return "third_down";
    return d > 0 ? "leap_up" : "leap_down";
}

// Scores every event whose departure is decided by this call: it has a next
// event, and it or its next event contains a note at index >= firstScoredNote
// (earlier events were scored when their phrase was chosen).
inline NoteMapResult evaluate_note_map(const std::vector<TrackNote>& track,
                                       int firstScoredNote,
                                       ChordLookup& chords,
                                       const Scale& scale,
                                       float beatsPerBar,
                                       const MelodyProfile& prof,
                                       bool keepVerdicts) {
    NoteMapResult r;
    const auto ev = merge_events(track);
    const int E = int(ev.size());
    for (int i = 0; i < E; ++i) {
        const PitchEvent& e = ev[i];
        if (i + 1 >= E) continue;                      // departure unknown
        const PitchEvent& nx = ev[i + 1];
        if (e.lastNote < firstScoredNote && nx.firstNote < firstScoredNote)
            continue;                                  // decided earlier
        const float nn = note_number_of_grid(e.grid, scale);
        const ScaleChord* rawOn  = chords.raw(e.onset);
        const ScaleChord* rawEnd = chords.raw(e.end - 1e-3f);
        const Chord* cOn  = chords.resolved(e.onset);
        const Chord* cEnd = chords.resolved(e.end - 1e-3f);
        if (!cOn || !cEnd) continue;
        const int mOn  = ChordLookup::member(nn, *cOn);
        const int mEnd = ChordLookup::member(nn, *cEnd);
        const int dep = nx.grid - e.grid;
        const bool hasPrev = i > 0;
        const int app = hasPrev ? e.grid - ev[i - 1].grid : 0;

        std::string kind;
        double odds = 100.0;
        bool classified = false;
        if (rawOn != rawEnd && mOn > 0 && mEnd == 0) {
            // Held from a chord where it belonged into one where it doesn't.
            kind = (dep == -1) ? "suspension" : "unresolvedSuspension";
            odds = (dep == -1) ? prof.nct.suspension : prof.nct.unresolvedSuspension;
            float ncDur = 0.0f;
            for (int k = e.firstNote; k <= e.lastNote; ++k)
                if (chords.raw(track[k].beat) == rawEnd) ncDur += track[k].duration;
            if (ncDur >= 2.0f) odds = prof.nct.longOdds;
            classified = true;
        } else if (mEnd > 0) {
            if (rawOn != rawEnd && mOn == 0) {
                if (!hasPrev) continue;
                // NCT at onset that becomes a chord tone when the next chord
                // arrives: anticipation (stepped into) or leapt-into.
                kind = (std::abs(app) == 1) ? "anticipation" : "appoggiatura";
                odds = (std::abs(app) == 1) ? prof.nct.anticipation : prof.nct.appoggiatura;
                classified = true;
            } else {
                const std::string key =
                    tendency_chord_label(*rawEnd) + ":" + std::to_string(mEnd);
                auto it = prof.tendencies.find(key);
                if (it != prof.tendencies.end()) {
                    const char* mot = motion_name(dep);
                    kind = "tendency " + key + " " + mot;
                    odds = it->second.odds_per_100(mot);
                    classified = true;
                }
            }
        } else {
            // Non-chord tone (of the chord at its end; same chord or NCT in both).
            if (!hasPrev) continue;
            const bool stepIn = std::abs(app) == 1, stepOut = std::abs(dep) == 1;
            if (stepIn && stepOut)       { kind = (app * dep > 0) ? "passing" : "neighbor";
                                           odds = (app * dep > 0) ? prof.nct.passing : prof.nct.neighbor; }
            else if (!stepIn && stepOut) { kind = "appoggiatura"; odds = prof.nct.appoggiatura; }
            else if (stepIn && !stepOut) { kind = "escape";       odds = prof.nct.escape; }
            else                         { kind = "free";         odds = prof.nct.free; }
            const float barPos = std::fmod(e.onset, beatsPerBar);
            if (barPos < 1e-4f || beatsPerBar - barPos < 1e-4f) odds *= prof.nct.accentedFactor;
            if (e.end - e.onset >= 2.0f) odds = prof.nct.longOdds;
            classified = true;
        }
        if (!classified) continue;
        const bool departure = odds < prof.departureBelow;
        r.logScore += std::log(std::max(odds, 1e-6) / 100.0);
        if (departure) ++r.departures;
        if (keepVerdicts) r.verdicts.push_back({i, e.onset, e.grid, kind, odds, departure});
    }
    return r;
}

} // namespace mforce
