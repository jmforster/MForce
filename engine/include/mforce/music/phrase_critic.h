#pragma once
// Phrase critic (walk3 spec 2026-09-22 §2) + passage range + the top-k pick
// shared by the phrase and passage levels (§3). Whole-phrase judgments that
// no single note choice owns. The gap-fill, leap-timing, final-note
// regression and penultimate terms are the walk2 anchor_selector formulas,
// unchanged, with their constants read from the profile.
#include "mforce/music/note_map.h"
#include "mforce/music/melody_profile.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

namespace mforce {

struct PhraseNote { int grid; float beat; float duration; int fig; };

struct CriticTerms {
    double rep{0}, motion{0}, range{0}, gap{0}, leap{0}, regress{0}, penult{0};
    double total() const { return rep + motion + range + gap + leap + regress + penult; }
};

// `leads` = the chain's connector lead steps (figure 1..F-1). `prior` = the
// passage's notes before this phrase (for the regression mean).
inline CriticTerms score_phrase_critic(const std::vector<PhraseNote>& ph,
                                       const std::vector<int>& leads,
                                       const std::vector<TrackNote>& prior,
                                       bool isPassageFinal,
                                       float passageTotalBeats,
                                       ChordLookup& chords,
                                       const MelodyCritic& c) {
    CriticTerms t;
    const int N = int(ph.size());
    if (N == 0) return t;
    // Range.
    int lo = ph[0].grid, hi = ph[0].grid;
    for (const auto& n : ph) { lo = std::min(lo, n.grid); hi = std::max(hi, n.grid); }
    const double span = double(hi - lo);
    if (span > c.rangeMaxSteps) t.range = -c.rangeCostPerStep * (span - c.rangeMaxSteps);
    // Motion (repeat fraction of moves).
    if (N >= 2) {
        int reps = 0;
        for (int i = 1; i < N; ++i) if (ph[i].grid == ph[i - 1].grid) ++reps;
        const double frac = double(reps) / double(N - 1);
        if (frac > c.motionRepeatFrac) t.motion = -c.motionCost * (frac - c.motionRepeatFrac);
    }
    // Repetition across a harmony change (> repMaxBeats of one pitch).
    {
        int runStart = 0;
        for (int i = 1; i <= N; ++i) {
            if (i == N || ph[i].grid != ph[runStart].grid) {
                float runDur = 0.0f;
                for (int k = runStart; k < i; ++k) runDur += ph[k].duration;
                if (runDur > c.repMaxBeats
                    && chords.raw(ph[runStart].beat)
                       != chords.raw(ph[i - 1].beat + ph[i - 1].duration - 1e-3f))
                    t.rep += c.repAcrossHarmony;
                runStart = i;
            }
        }
    }
    // Penultimate == final (passage end only).
    if (isPassageFinal && N >= 2 && ph[N - 1].grid == ph[N - 2].grid)
        t.penult += c.penultEqualsFinal;
    // Gap-fill: leap (>=3) then stepwise (<=2) reversal.
    for (int i = 1; i + 1 < N; ++i) {
        const int d1 = ph[i].grid - ph[i - 1].grid;
        const int d2 = ph[i + 1].grid - ph[i].grid;
        if (std::abs(d1) >= 3 && d1 * d2 < 0 && std::abs(d2) <= 2) t.gap += c.gapFill;
    }
    // Leap timing: connector cost by the figure's position in the passage.
    for (int f = 1; f <= int(leads.size()); ++f) {
        float figBeat = ph[0].beat;
        for (const auto& n : ph) if (n.fig == f) { figBeat = n.beat; break; }
        double factor = c.leapMid;
        if (passageTotalBeats > 0.0f) {
            if (figBeat < 0.5f * passageTotalBeats)        factor = c.leapEarly;
            else if (figBeat >= 0.75f * passageTotalBeats) factor = c.leapLate;
        }
        t.leap -= factor * std::abs(leads[f - 1]);
    }
    // Final-note regression toward the melody's mean.
    if (isPassageFinal) {
        double sum = 0.0; long long cnt = 0;
        for (const auto& p : prior) { sum += p.grid; ++cnt; }
        for (int i = 0; i < N - 1; ++i) { sum += ph[i].grid; ++cnt; }
        if (cnt > 0) {
            const double dist = std::abs(double(ph[N - 1].grid) - sum / double(cnt));
            t.regress -= c.regressPerStep * std::max(0.0, dist - c.regressSlack);
        }
    }
    return t;
}

inline double passage_range_term(const std::vector<TrackNote>& all, const MelodyCritic& c) {
    if (all.empty()) return 0.0;
    int lo = all[0].grid, hi = all[0].grid;
    for (const auto& n : all) { lo = std::min(lo, n.grid); hi = std::max(hi, n.grid); }
    const double span = double(hi - lo);
    return span > c.rangeMaxSteps ? -c.rangeCostPerStep * (span - c.rangeMaxSteps) : 0.0;
}

struct RankItem { bool inBudget; double score; };

// Rank by (inBudget desc, score desc); roulette exp(score - max) over the
// top k. Over-budget items can win only when nothing is in budget.
inline size_t pick_top_k(const std::vector<RankItem>& items, int k, Randomizer& rng) {
    std::vector<size_t> order(items.size());
    std::iota(order.begin(), order.end(), size_t(0));
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        if (items[a].inBudget != items[b].inBudget) return items[a].inBudget;
        return items[a].score > items[b].score;
    });
    size_t pool = std::min<size_t>(size_t(std::max(1, k)), order.size());
    // Never mix budget classes inside the pool.
    const bool topIn = items[order[0]].inBudget;
    size_t same = 0;
    while (same < pool && items[order[same]].inBudget == topIn) ++same;
    pool = same;
    double mx = items[order[0]].score;
    std::vector<double> w(pool);
    double tot = 0.0;
    for (size_t i = 0; i < pool; ++i) { w[i] = std::exp(items[order[i]].score - mx); tot += w[i]; }
    double draw = double(rng.value()) * tot;
    for (size_t i = 0; i + 1 < pool; ++i) { draw -= w[i]; if (draw <= 0.0) return order[i]; }
    return order[pool - 1];
}

} // namespace mforce
