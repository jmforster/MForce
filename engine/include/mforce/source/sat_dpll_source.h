#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// SatDpllSource — audio-rate DPLL (Davis-Putnam-Logemann-Loveland) SAT solver
// walking a decision tree over a fixed-size 3-SAT instance. Each integration
// step advances the solver by one step: assign next unassigned variable,
// propagate a conflict check, or backtrack. The sample is computed from the
// solver's current state (decision depth and partial-assignment parity),
// with audible transients on backtrack and regenerate events.
//
// The rhythm is shaped by the clause-to-variable ratio (phase transition of
// random 3-SAT near ~4.26): below that most instances are SAT (many slow
// solves, long runs at depth), above that most are UNSAT (noisy, click-laden
// backtrack storms).
// ---------------------------------------------------------------------------
struct SatDpllSource final : ValueSource {
  static constexpr int kMaxVars = 12;
  static constexpr int kMaxClauses = 48;

  int varCount{10};
  int clauseCount{30};
  int outputMode{0}; // 0 = signedDepth, 1 = parity ±1, 2 = abs depth

  SatDpllSource(int sampleRate, uint32_t seed = 0x5A71'D911u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(0.5f)),
    rate_(std::make_shared<ConstantSource>(48000.0f)),
    clauseDensity_(std::make_shared<ConstantSource>(3.0f)),
    clickAmount_(std::make_shared<ConstantSource>(0.3f)),
    smoothness_(std::make_shared<ConstantSource>(0.0f))
  {
    regenerate_instance();
    reset_solver();
  }

  const char* type_name() const override { return "SatDpllSource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",     0.5f,     0.0f,    10.0f},
      {"rate",          48000.0f, 0.0f,    500000.0f},
      {"clauseDensity", 3.0f,     0.5f,    10.0f},
      {"clickAmount",   0.3f,     0.0f,    1.0f},
      {"smoothness",    0.0f,     0.0f,    0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")     { amplitude_     = std::move(src); return; }
    if (name == "rate")          { rate_          = std::move(src); return; }
    if (name == "clauseDensity") { clauseDensity_ = std::move(src); return; }
    if (name == "clickAmount")   { clickAmount_   = std::move(src); return; }
    if (name == "smoothness")    { smoothness_    = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")     return amplitude_;
    if (name == "rate")          return rate_;
    if (name == "clauseDensity") return clauseDensity_;
    if (name == "clickAmount")   return clickAmount_;
    if (name == "smoothness")    return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"varCount",    ConfigType::Int, 10.0f, 4.0f, float(kMaxVars)},
      {"clauseCount", ConfigType::Int, 30.0f, 4.0f, float(kMaxClauses)},
      {"outputMode",  ConfigType::Int, 0.0f,  0.0f, 2.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "varCount") {
      int n = std::clamp(int(value), 4, kMaxVars);
      if (n != varCount) { varCount = n; regenerate_instance(); reset_solver(); }
      return;
    }
    if (name == "clauseCount") {
      int n = std::clamp(int(value), 4, kMaxClauses);
      if (n != clauseCount) { clauseCount = n; regenerate_instance(); reset_solver(); }
      return;
    }
    if (name == "outputMode") { outputMode = std::clamp(int(value), 0, 2); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "varCount")    return float(varCount);
    if (name == "clauseCount") return float(clauseCount);
    if (name == "outputMode")  return float(outputMode);
    return 0.0f;
  }

  void prepare(int frames) override {
    if (amplitude_)     amplitude_->prepare(frames);
    if (rate_)          rate_->prepare(frames);
    if (clauseDensity_) clauseDensity_->prepare(frames);
    if (clickAmount_)   clickAmount_->prepare(frames);
    if (smoothness_)    smoothness_->prepare(frames);
  }

  float next() override {
    amplitude_->next();
    rate_->next();
    clauseDensity_->next();
    clickAmount_->next();
    smoothness_->next();

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    // Clear per-sample event flags before stepping (so events within this
    // sample window stick into the output below).
    backtrackEvent_ = false;
    regenerateEvent_ = false;

    while (opCredits_ >= 1.0f) {
      integrate_one_step();
      opCredits_ -= 1.0f;
    }

    const int depth = trailTop_;
    int posCount = 0;
    for (int i = 0; i < varCount; ++i) {
      if (assignment_[i] == 1) ++posCount;
    }
    const bool parity = (posCount & 1) != 0;
    const int signedDepth = parity ? -depth : depth;

    float raw = 0.0f;
    switch (outputMode) {
      case 0:
        raw = float(signedDepth) / float(kMaxVars);
        break;
      case 1:
        raw = parity ? -1.0f : 1.0f;
        break;
      case 2:
        raw = float(depth) / float(kMaxVars);
        break;
    }

    if (backtrackEvent_) {
      float click = std::clamp(clickAmount_->current(), 0.0f, 1.0f);
      float sgn = (raw >= 0.0f) ? 1.0f : -1.0f;
      raw += click * sgn;
    }
    if (regenerateEvent_) {
      float sgn = (raw >= 0.0f) ? 1.0f : -1.0f;
      raw = sgn * 1.0f;
    }

    raw = std::clamp(raw, -1.5f, 1.5f);

    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  // Literal encoding: bit 7 = sign (1 = negated), bits 0..3 = variable index.
  static constexpr uint8_t kLitUnused = 0xFFu;

  static uint8_t make_lit(int varIdx, bool negated) {
    return uint8_t((negated ? 0x80u : 0x00u) | uint8_t(varIdx & 0x0F));
  }
  static int lit_var(uint8_t l)  { return int(l & 0x0F); }
  static bool lit_neg(uint8_t l) { return (l & 0x80u) != 0; }

  // Returns: +1 if clause has a satisfied literal, 0 if unresolved (has an
  // unassigned literal and no satisfied one), -1 if all literals are falsified.
  int clause_status(int c) const {
    bool hasUnassigned = false;
    for (int k = 0; k < 3; ++k) {
      uint8_t lit = clauseLit_[c][k];
      if (lit == kLitUnused) continue;
      int v = lit_var(lit);
      int a = assignment_[v];
      if (a == 0) { hasUnassigned = true; continue; }
      bool lv = lit_neg(lit) ? (a == -1) : (a == 1);
      if (lv) return 1;
    }
    return hasUnassigned ? 0 : -1;
  }

  // One step of DPLL:
  //  - if any clause is falsified: backtrack (flip-if-untried, else pop).
  //  - else if all clauses satisfied: mark regenerate and restart.
  //  - else pick the lowest-index unassigned variable, try true first (or
  //    the untried branch value if we returned here by backtracking).
  void integrate_one_step() {
    // 1. Conflict / satisfaction check.
    bool anyUnresolved = false;
    for (int c = 0; c < activeClauses_; ++c) {
      int st = clause_status(c);
      if (st < 0) {
        backtrack();
        ++stepsSinceProgress_;
        watchdog_check();
        return;
      }
      if (st == 0) anyUnresolved = true;
    }

    if (!anyUnresolved) {
      // Full assignment satisfies all clauses: victory transient, restart.
      regenerateEvent_ = true;
      regenerate_instance();
      reset_solver();
      stepsSinceProgress_ = 0;
      return;
    }

    // 2. Pick next unassigned variable (lowest index).
    int pick = -1;
    for (int v = 0; v < varCount; ++v) {
      if (assignment_[v] == 0) { pick = v; break; }
    }
    if (pick < 0) {
      // No unassigned but also unresolved — shouldn't happen, but guard.
      regenerateEvent_ = true;
      regenerate_instance();
      reset_solver();
      stepsSinceProgress_ = 0;
      return;
    }

    // Decide branch value: prefer true if untried, else false, else backtrack.
    int depth = trailTop_;
    bool tryTrue  = !tried_[depth][0];
    bool tryFalse = !tried_[depth][1];
    if (!tryTrue && !tryFalse) {
      backtrack();
      ++stepsSinceProgress_;
      watchdog_check();
      return;
    }

    bool val = tryTrue;
    assignment_[pick] = val ? 1 : -1;
    tried_[depth][val ? 0 : 1] = true;
    trail_[depth] = int8_t(pick);
    ++trailTop_;
    stepsSinceProgress_ = 0;
  }

  void backtrack() {
    backtrackEvent_ = true;
    while (trailTop_ > 0) {
      int depth = trailTop_ - 1;
      int v = trail_[depth];
      int curVal = assignment_[v];
      bool triedTrue  = tried_[depth][0];
      bool triedFalse = tried_[depth][1];
      if (triedTrue && !triedFalse) {
        // Flip to false in place; keep depth.
        assignment_[v] = -1;
        tried_[depth][1] = true;
        return;
      }
      if (!triedTrue && triedFalse) {
        assignment_[v] = 1;
        tried_[depth][0] = true;
        return;
      }
      // Both branches exhausted at this depth: pop.
      assignment_[v] = 0;
      tried_[depth][0] = false;
      tried_[depth][1] = false;
      --trailTop_;
      (void)curVal;
    }
    // Trail empty: entire tree exhausted => UNSAT. Regenerate.
    regenerateEvent_ = true;
    regenerate_instance();
    reset_solver();
    stepsSinceProgress_ = 0;
  }

  void watchdog_check() {
    const int limit = 2 * kMaxVars * kMaxClauses;
    if (stepsSinceProgress_ > limit) {
      regenerateEvent_ = true;
      regenerate_instance();
      reset_solver();
      stepsSinceProgress_ = 0;
    }
  }

  void reset_solver() {
    for (int i = 0; i < kMaxVars; ++i) {
      assignment_[i] = 0;
      trail_[i] = 0;
      tried_[i][0] = false;
      tried_[i][1] = false;
    }
    trailTop_ = 0;
    stepsSinceProgress_ = 0;
  }

  void regenerate_instance() {
    // Reconcile clauseCount with the current clauseDensity param (param wins).
    int targetClauses = clauseCount;
    if (clauseDensity_) {
      float density = std::clamp(clauseDensity_->current(), 0.5f, 10.0f);
      int fromDensity = int(std::lround(density * float(varCount)));
      targetClauses = std::clamp(fromDensity, 4, kMaxClauses);
    }
    activeClauses_ = std::clamp(targetClauses, 4, kMaxClauses);

    for (int c = 0; c < kMaxClauses; ++c) {
      for (int k = 0; k < 3; ++k) clauseLit_[c][k] = kLitUnused;
    }

    const int vc = std::max(3, varCount);
    for (int c = 0; c < activeClauses_; ++c) {
      // Sample 3 distinct variables.
      int v0 = rng_.int_range(0, vc - 1);
      int v1;
      do { v1 = rng_.int_range(0, vc - 1); } while (v1 == v0);
      int v2;
      do { v2 = rng_.int_range(0, vc - 1); } while (v2 == v0 || v2 == v1);
      clauseLit_[c][0] = make_lit(v0, rng_.decide(0.5f));
      clauseLit_[c][1] = make_lit(v1, rng_.decide(0.5f));
      clauseLit_[c][2] = make_lit(v2, rng_.decide(0.5f));
    }
  }

  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_, rate_, clauseDensity_, clickAmount_, smoothness_;

  // DPLL state.
  int8_t  assignment_[kMaxVars]{};
  int8_t  trail_[kMaxVars]{};
  bool    tried_[kMaxVars][2]{};
  uint8_t clauseLit_[kMaxClauses][3]{};
  int     trailTop_{0};
  int     activeClauses_{0};
  int     stepsSinceProgress_{0};

  // Per-sample event flags set by integrate_one_step / backtrack.
  bool backtrackEvent_{false};
  bool regenerateEvent_{false};

  float opCredits_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
