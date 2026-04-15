#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

namespace mforce {

// ---------------------------------------------------------------------------
// MassSpringSource — 1D network of point masses coupled by springs.
//
// Unlike FHN / Gray-Scott / sort oscillators (which are essentially shaped
// noise — no natural period), a mass-spring network has genuine modal
// resonance: pluck it and it rings at eigenfrequencies of the connectivity
// graph. That's why physically-modelled strings sound pitched.
//
// The twist here is topological surgery: under enough tension springs
// BREAK, and (optionally) nearby masses RE-FORM new springs. So the
// "instrument" can literally rebuild its own body while it's ringing.
// Pluck softly → a pitched chain. Pluck hard → springs snap, new ones form,
// the topology (and therefore the mode structure) drifts. "Pluck it hard
// enough and it becomes a different instrument."
//
// State per mass i: position x_i, velocity v_i, rest position rest_i.
// Forces: external spring toward rest + every connected spring's Hooke
// force. Integrator: semi-implicit Euler (velocity updated first, then
// position) — stable under damping.
// ---------------------------------------------------------------------------
struct MassSpringSource final : ValueSource {
  struct Spring { int a; int b; float restLen; };

  int nodeCount{16};
  int pluckNode{0};
  int listenNode{5};
  int reformMode{1}; // 0 = break-only, 1 = reform to nearest within radius

  MassSpringSource(int sampleRate, uint32_t seed = 0xA455'0001u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(1.0f)),
    rate_(std::make_shared<ConstantSource>(48000.0f)),
    dt_(std::make_shared<ConstantSource>(0.0005f)),
    stiffness_(std::make_shared<ConstantSource>(80.0f)),
    damping_(std::make_shared<ConstantSource>(0.5f)),
    kExternal_(std::make_shared<ConstantSource>(0.1f)),
    breakThreshold_(std::make_shared<ConstantSource>(0.3f)),
    reformRadius_(std::make_shared<ConstantSource>(0.2f)),
    pluckAmount_(std::make_shared<ConstantSource>(1.0f)),
    smoothness_(std::make_shared<ConstantSource>(0.0f))
  {
    resize_and_reset(nodeCount);
  }

  const char* type_name() const override { return "MassSpringSource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",      1.0f,     0.0f,   10.0f},
      {"rate",           48000.0f, 0.0f,   2000000.0f},
      {"dt",             0.0005f,  0.0f,   0.01f},
      {"stiffness",      80.0f,    0.0f,   10000.0f},
      {"damping",        0.5f,     0.0f,   100.0f},
      {"kExternal",      0.1f,     0.0f,   100.0f},
      {"breakThreshold", 0.3f,     0.0f,   10.0f},
      {"reformRadius",   0.2f,     0.0f,   2.0f},
      {"pluckAmount",    1.0f,    -10.0f,  10.0f},
      {"smoothness",     0.0f,     0.0f,   0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")      { amplitude_      = std::move(src); return; }
    if (name == "rate")           { rate_           = std::move(src); return; }
    if (name == "dt")             { dt_             = std::move(src); return; }
    if (name == "stiffness")      { stiffness_      = std::move(src); return; }
    if (name == "damping")        { damping_        = std::move(src); return; }
    if (name == "kExternal")      { kExternal_      = std::move(src); return; }
    if (name == "breakThreshold") { breakThreshold_ = std::move(src); return; }
    if (name == "reformRadius")   { reformRadius_   = std::move(src); return; }
    if (name == "pluckAmount")    { pluckAmount_    = std::move(src); return; }
    if (name == "smoothness")     { smoothness_     = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")      return amplitude_;
    if (name == "rate")           return rate_;
    if (name == "dt")             return dt_;
    if (name == "stiffness")      return stiffness_;
    if (name == "damping")        return damping_;
    if (name == "kExternal")      return kExternal_;
    if (name == "breakThreshold") return breakThreshold_;
    if (name == "reformRadius")   return reformRadius_;
    if (name == "pluckAmount")    return pluckAmount_;
    if (name == "smoothness")     return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"nodeCount",  ConfigType::Int, 16.0f, 4.0f, 64.0f},
      {"pluckNode",  ConfigType::Int, 0.0f,  0.0f, 63.0f},
      {"listenNode", ConfigType::Int, 5.0f,  0.0f, 63.0f},
      {"reformMode", ConfigType::Int, 1.0f,  0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "nodeCount") {
      int n = std::clamp(int(value), 4, 64);
      if (n != nodeCount) { nodeCount = n; resize_and_reset(nodeCount); }
      return;
    }
    if (name == "pluckNode")  { pluckNode  = std::clamp(int(value), 0, std::max(0, nodeCount - 1)); return; }
    if (name == "listenNode") { listenNode = std::clamp(int(value), 0, std::max(0, nodeCount - 1)); return; }
    if (name == "reformMode") { reformMode = std::clamp(int(value), 0, 1); return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "nodeCount")  return float(nodeCount);
    if (name == "pluckNode")  return float(pluckNode);
    if (name == "listenNode") return float(listenNode);
    if (name == "reformMode") return float(reformMode);
    return 0.0f;
  }

  void prepare(int frames) override {
    if (amplitude_)      amplitude_->prepare(frames);
    if (rate_)           rate_->prepare(frames);
    if (dt_)             dt_->prepare(frames);
    if (stiffness_)      stiffness_->prepare(frames);
    if (damping_)        damping_->prepare(frames);
    if (kExternal_)      kExternal_->prepare(frames);
    if (breakThreshold_) breakThreshold_->prepare(frames);
    if (reformRadius_)   reformRadius_->prepare(frames);
    if (pluckAmount_)    pluckAmount_->prepare(frames);
    if (smoothness_)     smoothness_->prepare(frames);
  }

  float next() override {
    amplitude_->next(); rate_->next(); dt_->next();
    stiffness_->next(); damping_->next(); kExternal_->next();
    breakThreshold_->next(); reformRadius_->next();
    pluckAmount_->next(); smoothness_->next();

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    while (opCredits_ >= 1.0f) {
      integrate_one_step();
      opCredits_ -= 1.0f;
    }

    const int N = int(x_.size());
    int ln = std::clamp(listenNode, 0, std::max(0, N - 1));
    float raw = (N > 0) ? (x_[ln] - rest_[ln]) : 0.0f;

    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

  // Explicit re-pluck: displace pluckNode again without rebuilding topology.
  void repluck() {
    const int N = int(x_.size());
    if (N == 0) return;
    int pn = std::clamp(pluckNode, 0, N - 1);
    x_[pn] += pluckAmount_ ? pluckAmount_->current() : 1.0f;
  }

private:
  // -------------------------------------------------------------------------
  // Integration: semi-implicit Euler with optional topological surgery.
  // -------------------------------------------------------------------------
  void integrate_one_step() {
    const int N = int(x_.size());
    if (N == 0) return;

    const float dt     = std::max(0.0f, dt_->current());
    const float k      = std::max(0.0f, stiffness_->current());
    const float damp   = std::max(0.0f, damping_->current());
    const float kExt   = std::max(0.0f, kExternal_->current());
    const float brkT   = std::max(0.0f, breakThreshold_->current());
    const float reformR = std::max(0.0f, reformRadius_->current());

    // Accumulate forces. Start with the soft external pullback toward rest
    // — this keeps the centre of mass from drifting off to infinity once
    // the topology has been shredded and the network is no longer closed.
    force_.assign(size_t(N), 0.0f);
    for (int i = 0; i < N; ++i) {
      force_[i] = -kExt * (x_[i] - rest_[i]);
    }

    // Spring forces + mark broken springs. We scan once, compute dx,
    // apply Hooke's law to both endpoints, and tag springs whose
    // displacement magnitude exceeds breakThreshold for removal.
    brokenMarks_.assign(springs_.size(), 0);
    brokenEndpoints_.clear();
    for (size_t s = 0; s < springs_.size(); ++s) {
      const Spring& sp = springs_[s];
      float dx = x_[sp.b] - x_[sp.a];
      float stretch = dx - sp.restLen;
      float springForce = k * stretch;
      force_[sp.a] += springForce;
      force_[sp.b] -= springForce;
      if (std::fabs(stretch) > brkT) {
        brokenMarks_[s] = 1;
        brokenEndpoints_.push_back(sp.a);
        brokenEndpoints_.push_back(sp.b);
      }
    }

    // Semi-implicit Euler: update velocity first (with damping applied as
    // a linear drag), then update position using the new velocity.
    for (int i = 0; i < N; ++i) {
      v_[i] += dt * (force_[i] - damp * v_[i]); // mass = 1
      x_[i] += dt * v_[i];
      // NaN guard: if anything exploded, snap this mass back to rest.
      if (!std::isfinite(x_[i]) || !std::isfinite(v_[i])) {
        x_[i] = rest_[i];
        v_[i] = 0.0f;
      }
      x_[i] = std::clamp(x_[i], -10.0f, 10.0f);
    }

    // Remove marked springs (compact in place).
    if (!brokenEndpoints_.empty()) {
      size_t w = 0;
      for (size_t r = 0; r < springs_.size(); ++r) {
        if (!brokenMarks_[r]) {
          if (w != r) springs_[w] = springs_[r];
          ++w;
        }
      }
      springs_.resize(w);
    }

    // Reform: for each endpoint of a just-broken spring, try to connect
    // it to its nearest currently-unconnected neighbour within reformR.
    // We only allow each mass to reform once per step to avoid runaway
    // densification / instability.
    if (reformMode == 1 && !brokenEndpoints_.empty()) {
      reformedThisStep_.assign(size_t(N), 0);
      for (int idx : brokenEndpoints_) {
        if (idx < 0 || idx >= N) continue;
        if (reformedThisStep_[idx]) continue;
        int best = -1;
        float bestDist = reformR;
        for (int j = 0; j < N; ++j) {
          if (j == idx) continue;
          float d = std::fabs(x_[j] - x_[idx]);
          if (d <= 0.0f) continue;
          if (d >= bestDist) continue;
          if (has_spring(idx, j)) continue;
          if (reformedThisStep_[j]) continue;
          best = j;
          bestDist = d;
        }
        if (best >= 0) {
          Spring ns;
          ns.a = std::min(idx, best);
          ns.b = std::max(idx, best);
          ns.restLen = x_[ns.b] - x_[ns.a]; // start at zero tension
          springs_.push_back(ns);
          reformedThisStep_[idx] = 1;
          reformedThisStep_[best] = 1;
        }
      }
    }
  }

  bool has_spring(int a, int b) const {
    int lo = std::min(a, b), hi = std::max(a, b);
    for (const Spring& s : springs_) {
      int sl = std::min(s.a, s.b), sh = std::max(s.a, s.b);
      if (sl == lo && sh == hi) return true;
    }
    return false;
  }

  // -------------------------------------------------------------------------
  // Topology construction: uniformly spaced rest positions in (0,1),
  // ring connectivity, silent initial velocity, then pluck the pluckNode.
  // -------------------------------------------------------------------------
  void resize_and_reset(int n) {
    n = std::clamp(n, 4, 64);
    x_.assign(size_t(n), 0.0f);
    v_.assign(size_t(n), 0.0f);
    rest_.assign(size_t(n), 0.0f);
    force_.assign(size_t(n), 0.0f);
    for (int i = 0; i < n; ++i) {
      rest_[i] = (float(i) + 0.5f) / float(n);
      x_[i] = rest_[i];
      v_[i] = 0.0f;
    }
    springs_.clear();
    for (int i = 0; i < n - 1; ++i) {
      Spring sp;
      sp.a = i;
      sp.b = i + 1;
      sp.restLen = rest_[i + 1] - rest_[i];
      springs_.push_back(sp);
    }
    // Close the ring. The wrap spring's "restLen" is the negative gap
    // spanning the whole chain — using the signed dx convention means a
    // straight chain sits at zero tension initially.
    if (n >= 2) {
      Spring wrap;
      wrap.a = n - 1;
      wrap.b = 0;
      wrap.restLen = rest_[0] - rest_[n - 1];
      springs_.push_back(wrap);
    }

    pluckNode  = std::clamp(pluckNode,  0, n - 1);
    listenNode = std::clamp(listenNode, 0, n - 1);

    // Initial pluck: displace pluckNode by pluckAmount so the network has
    // energy to start ringing from t=0.
    float amt = pluckAmount_ ? pluckAmount_->current() : 1.0f;
    if (amt == 0.0f) amt = 1.0f;
    x_[pluckNode] += amt;

    opCredits_ = 0.0f;
    smoothedPrev_ = 0.0f;
    cur_ = 0.0f;
  }

  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_, rate_, dt_;
  std::shared_ptr<ValueSource> stiffness_, damping_, kExternal_;
  std::shared_ptr<ValueSource> breakThreshold_, reformRadius_;
  std::shared_ptr<ValueSource> pluckAmount_, smoothness_;

  std::vector<float> x_, v_, rest_, force_;
  std::vector<Spring> springs_;

  // Scratch buffers reused across integration steps (avoid per-step allocs
  // in the hot loop).
  std::vector<uint8_t> brokenMarks_;
  std::vector<int> brokenEndpoints_;
  std::vector<uint8_t> reformedThisStep_;

  float opCredits_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
