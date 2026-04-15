#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <cmath>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// LDPCSource — Low-Density Parity-Check belief-propagation decoder running
// one message-passing iteration per integration step.
//
// A small fixed regular (3, 6) LDPC code with kN=16 variable nodes and
// kM=8 check nodes. A random codeword (the all-zeros codeword, which is
// always valid for a linear code) is transmitted across a simulated AWGN
// channel. Channel log-likelihood ratios (LLRs) drive min-sum BP.
//
// The SNR parameter steers the decoder into one of three regimes, each
// with its own timbral character:
//   - Well above threshold  → decoder converges quickly → goes silent
//                             (restarts produce periodic "ticks").
//   - Near threshold        → metastable pattering, sign flips cluster.
//   - Below threshold       → never converges, churns chaotically.
//
// dt is implicit (one iteration per step). The `rate` param = iterations
// per second, which sets how fast the simulated decoder evolves.
// ---------------------------------------------------------------------------
struct LDPCSource final : ValueSource {
  static constexpr int kN = 16;         // variable nodes
  static constexpr int kM = 8;          // check nodes
  static constexpr int kDv = 3;         // variable degree
  static constexpr int kDc = 6;         // check degree
  static constexpr float kMsgClamp = 20.0f;

  int outputMode{0};     // 0 = sign changes, 1 = mean posterior LLR,
                         // 2 = sign of LLR[0], 3 = hamming error rate
  int codePattern{0};    // 0 = regular (3,6), 1 = shuffled variant

  LDPCSource(int sampleRate, uint32_t seed = 0x1DBC'0001u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_(std::make_shared<ConstantSource>(0.5f)),
    rate_(std::make_shared<ConstantSource>(48000.0f)),
    snr_(std::make_shared<ConstantSource>(1.5f)),
    maxIter_(std::make_shared<ConstantSource>(20.0f)),
    smoothness_(std::make_shared<ConstantSource>(0.3f))
  {
    build_code();
    restart_channel();
  }

  const char* type_name() const override { return "LDPCSource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",  0.5f,     0.0f,    10.0f},
      {"rate",       48000.0f, 0.0f,    2000000.0f},
      {"snr",        1.5f,     0.01f,   20.0f},
      {"maxIter",    20.0f,    1.0f,    500.0f},
      {"smoothness", 0.3f,     0.0f,    0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")  { amplitude_  = std::move(src); return; }
    if (name == "rate")       { rate_       = std::move(src); return; }
    if (name == "snr")        { snr_        = std::move(src); return; }
    if (name == "maxIter")    { maxIter_    = std::move(src); return; }
    if (name == "smoothness") { smoothness_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")  return amplitude_;
    if (name == "rate")       return rate_;
    if (name == "snr")        return snr_;
    if (name == "maxIter")    return maxIter_;
    if (name == "smoothness") return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"outputMode",  ConfigType::Int, 0.0f, 0.0f, 3.0f},
      {"codePattern", ConfigType::Int, 0.0f, 0.0f, 1.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "outputMode")  { outputMode  = std::clamp(int(value), 0, 3); return; }
    if (name == "codePattern") {
      int p = std::clamp(int(value), 0, 1);
      if (p != codePattern) { codePattern = p; build_code(); restart_channel(); }
      return;
    }
  }

  float get_config(std::string_view name) const override {
    if (name == "outputMode")  return float(outputMode);
    if (name == "codePattern") return float(codePattern);
    return 0.0f;
  }

  void prepare(int frames) override {
    if (amplitude_)  amplitude_->prepare(frames);
    if (rate_)       rate_->prepare(frames);
    if (snr_)        snr_->prepare(frames);
    if (maxIter_)    maxIter_->prepare(frames);
    if (smoothness_) smoothness_->prepare(frames);
  }

  float next() override {
    amplitude_->next(); rate_->next();
    snr_->next(); maxIter_->next(); smoothness_->next();

    float rateHz = std::max(0.0f, rate_->current());
    opCredits_ += rateHz / float(sampleRate_);

    while (opCredits_ >= 1.0f) {
      integrate_one_step();
      opCredits_ -= 1.0f;
    }

    float raw = 0.0f;
    switch (outputMode) {
      case 0: {
        // normalized sign-change density, mapped into [-1, 1]
        float f = float(signChangesThisStep_) / float(kN);
        raw = std::clamp(2.0f * f - 1.0f, -1.0f, 1.0f);
        break;
      }
      case 1: {
        float s = 0.0f;
        for (int i = 0; i < kN; ++i) s += post_llr_[i];
        raw = std::tanh(s / float(kN));
        break;
      }
      case 2: {
        raw = (post_llr_[0] >= 0.0f) ? 1.0f : -1.0f;
        break;
      }
      case 3: {
        // transmitted codeword is all zeros; hard decision is 1 if llr<0.
        int err = 0;
        for (int i = 0; i < kN; ++i) if (post_llr_[i] < 0.0f) ++err;
        raw = float(err) / float(kN) - 0.5f;
        raw = std::clamp(raw * 2.0f, -1.0f, 1.0f);
        break;
      }
    }

    raw = std::clamp(raw, -1.0f, 1.0f);

    float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  // -------------------------------------------------------------------------
  // Build deterministic (3,6)-regular parity-check adjacency.
  //
  // Scheme: view H as a kM x kN = 8 x 16 matrix partitioned into three
  // 8 x 8 circulant blocks (we use two for a 6-per-check count, plus a
  // shifted repetition for the third variable connection). Specifically,
  // each check c connects to variables:
  //   { (c + 0) mod 8,           (c + 1) mod 8,           (c + 3) mod 8,
  //     8 + (c + 0) mod 8,       8 + (c + 2) mod 8,       8 + (c + 5) mod 8 }
  // Pattern 1 shuffles the offsets to produce a different girth profile.
  // Each variable v therefore sits in exactly kDv = 3 checks; by
  // construction each check sits on exactly kDc = 6 variables.
  // -------------------------------------------------------------------------
  void build_code() {
    int offA[3], offB[3];
    if (codePattern == 0) {
      offA[0] = 0; offA[1] = 1; offA[2] = 3;
      offB[0] = 0; offB[1] = 2; offB[2] = 5;
    } else {
      offA[0] = 0; offA[1] = 2; offA[2] = 5;
      offB[0] = 1; offB[1] = 3; offB[2] = 6;
    }

    // Fill checkAdj_ first, then derive varAdj_ by scanning checkAdj_.
    for (int c = 0; c < kM; ++c) {
      checkAdj_[c][0] = (c + offA[0]) % kM;
      checkAdj_[c][1] = (c + offA[1]) % kM;
      checkAdj_[c][2] = (c + offA[2]) % kM;
      checkAdj_[c][3] = kM + (c + offB[0]) % kM;
      checkAdj_[c][4] = kM + (c + offB[1]) % kM;
      checkAdj_[c][5] = kM + (c + offB[2]) % kM;
    }

    // Compute varAdj_ and the reciprocal index maps.
    int varDegCount[kN] = {0};
    for (int c = 0; c < kM; ++c) {
      for (int j = 0; j < kDc; ++j) {
        int v = checkAdj_[c][j];
        int slot = varDegCount[v]++;
        // Defensive — adjacency design guarantees slot in [0, kDv).
        if (slot < kDv) {
          varAdj_[v][slot] = c;
          varSlotInCheck_[v][slot] = j;
          checkSlotInVar_[c][j] = slot;
        }
      }
    }
    // If any variable came up short (shouldn't happen with valid offsets),
    // pad with self-references to avoid undefined reads.
    for (int v = 0; v < kN; ++v) {
      while (varDegCount[v] < kDv) {
        int slot = varDegCount[v]++;
        varAdj_[v][slot] = 0;
        varSlotInCheck_[v][slot] = 0;
      }
    }
  }

  void restart_channel() {
    // Transmit all-zeros codeword (always a valid codeword for a linear code).
    // BPSK map: bit 0 → +1, bit 1 → −1. Received y = x + noise.
    // Channel LLR for AWGN with variance σ² is 2y/σ².
    float snr = std::max(0.01f, snr_ ? snr_->current() : 1.5f);
    // Treat "snr" as linear Es/N0; σ² = 1/(2*snr) so noise σ = sqrt(1/(2*snr)).
    float sigma  = std::sqrt(1.0f / (2.0f * snr));
    float sigma2 = sigma * sigma;
    float scale  = 2.0f / sigma2;

    for (int v = 0; v < kN; ++v) {
      // Transmitted x = +1 (all-zeros codeword, BPSK).
      float y = 1.0f + rng_.valuePN() * sigma;
      llr_channel_[v] = std::clamp(scale * y, -kMsgClamp, kMsgClamp);
      post_llr_[v] = llr_channel_[v];
      prevSign_[v] = (llr_channel_[v] >= 0.0f) ? 1 : -1;
    }
    for (int c = 0; c < kM; ++c)
      for (int j = 0; j < kDc; ++j) msg_v2c_[c][j] = 0.0f;
    for (int v = 0; v < kN; ++v)
      for (int j = 0; j < kDv; ++j) msg_c2v_[v][j] = 0.0f;

    iter_ = 0;
    stuckIter_ = 0;
    signChangesThisStep_ = 0;
  }

  void integrate_one_step() {
    // ---- 1. Variable → Check ----
    // msg_v2c[c][j] where variable v = checkAdj_[c][j], slot s = checkSlotInVar_[c][j].
    // Message = llr_channel[v] + sum(msg_c2v[v][*]) − msg_c2v[v][s]
    float v_sum[kN];
    for (int v = 0; v < kN; ++v) {
      float s = llr_channel_[v];
      for (int j = 0; j < kDv; ++j) s += msg_c2v_[v][j];
      v_sum[v] = s;
    }
    for (int c = 0; c < kM; ++c) {
      for (int j = 0; j < kDc; ++j) {
        int v = checkAdj_[c][j];
        int s = checkSlotInVar_[c][j];
        float m = v_sum[v] - msg_c2v_[v][s];
        if (!std::isfinite(m)) m = 0.0f;
        msg_v2c_[c][j] = std::clamp(m, -kMsgClamp, kMsgClamp);
      }
    }

    // ---- 2. Check → Variable (min-sum) ----
    for (int c = 0; c < kM; ++c) {
      // Find min1, min2 of |msg_v2c[c][*]|, index of min1, and total sign product.
      float min1 = kMsgClamp * 10.0f, min2 = kMsgClamp * 10.0f;
      int   imin = 0;
      int   signProd = 1;
      for (int j = 0; j < kDc; ++j) {
        float x = msg_v2c_[c][j];
        if (x < 0.0f) signProd = -signProd;
        float a = std::fabs(x);
        if (a < min1) { min2 = min1; min1 = a; imin = j; }
        else if (a < min2) { min2 = a; }
      }
      for (int j = 0; j < kDc; ++j) {
        float x = msg_v2c_[c][j];
        int sOther = (x < 0.0f) ? -signProd : signProd; // exclude this edge's sign
        float magOther = (j == imin) ? min2 : min1;
        float out = float(sOther) * magOther;
        if (!std::isfinite(out)) out = 0.0f;
        out = std::clamp(out, -kMsgClamp, kMsgClamp);
        int v = checkAdj_[c][j];
        int slot = checkSlotInVar_[c][j];
        msg_c2v_[v][slot] = out;
      }
    }

    // ---- 3. Posterior LLR + sign-flip count ----
    int flips = 0;
    for (int v = 0; v < kN; ++v) {
      float s = llr_channel_[v];
      for (int j = 0; j < kDv; ++j) s += msg_c2v_[v][j];
      if (!std::isfinite(s)) s = llr_channel_[v];
      post_llr_[v] = std::clamp(s, -kMsgClamp, kMsgClamp);
      int sg = (post_llr_[v] >= 0.0f) ? 1 : -1;
      if (sg != prevSign_[v]) ++flips;
      prevSign_[v] = sg;
    }
    signChangesThisStep_ = flips;

    // ---- 4. Convergence & restart bookkeeping ----
    ++iter_;
    if (flips == 0) ++stuckIter_; else stuckIter_ = 0;

    bool converged = all_parity_checks_satisfied();
    int maxIt = std::max(1, int(maxIter_ ? maxIter_->current() : 20.0f));

    // Stuck-without-convergence guard: if we've been flat for >= maxIter
    // iterations but not converged, restart with fresh channel noise.
    bool stuckTooLong = (stuckIter_ >= maxIt) && !converged;

    if (converged || iter_ >= maxIt || stuckTooLong) {
      restart_channel();
    }
  }

  bool all_parity_checks_satisfied() const {
    // Hard decision from post_llr_ (bit = 1 iff llr < 0); check every parity.
    for (int c = 0; c < kM; ++c) {
      int parity = 0;
      for (int j = 0; j < kDc; ++j) {
        int v = checkAdj_[c][j];
        if (post_llr_[v] < 0.0f) parity ^= 1;
      }
      if (parity != 0) return false;
    }
    return true;
  }

  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_, rate_;
  std::shared_ptr<ValueSource> snr_, maxIter_, smoothness_;

  // Channel / decoder state.
  float llr_channel_[kN]{};
  float post_llr_[kN]{};
  float msg_v2c_[kM][kDc]{};    // rows indexed by check, slot = position in check
  float msg_c2v_[kN][kDv]{};    // rows indexed by variable, slot = position in variable
  int   prevSign_[kN]{};

  // Adjacency (built once in build_code()).
  int varAdj_[kN][kDv]{};              // which checks each variable is in
  int checkAdj_[kM][kDc]{};            // which variables each check sees
  int varSlotInCheck_[kN][kDv]{};      // for varAdj_[v][j] = c, the slot within check c
  int checkSlotInVar_[kM][kDc]{};      // for checkAdj_[c][j] = v, the slot within variable v

  int iter_{0};
  int stuckIter_{0};
  int signChangesThisStep_{0};

  float opCredits_{0.0f};
  float smoothedPrev_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
