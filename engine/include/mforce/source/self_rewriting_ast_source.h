#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>

namespace mforce {

// ---------------------------------------------------------------------------
// SelfRewritingASTSource — an oscillator whose signal is produced by
// evaluating a tiny arithmetic/trig expression tree over a time variable `t`.
// Periodically (every 1/rewriteRate seconds) one rewrite rule is applied to
// mutate the tree: wrap a subtree in sin/cos/tanh, scale it, add a harmonic,
// or tweak a constant. Because the tree holds periodic sub-expressions like
// sin(ωt), the output stays pitched; rewrites modulate timbre.
//
//   Fixed points  (rare rewrites) → tonal, slowly drifting
//   Cycling rules (moderate rate) → periodic timbral shifts
//   Chaotic rules (high rate)     → vocal / fricative texture
//
// Everything lives in a fixed-size node pool — no heap allocation in the
// audio render loop. All tree manipulation uses pool indices.
// ---------------------------------------------------------------------------

struct SelfRewritingASTSource final : ValueSource {
  // Node pool is fixed at 64. Keep as constexpr so it's baked into state.
  static constexpr int kPoolSize   = 64;
  static constexpr int kMaxDepth   = 64;   // hard ceiling; runtime depthLimit clamps under this.

  int depthLimit{32};  // runtime eval recursion cap
  int seedMode{0};     // 0 = Mul(0.5, Sin(t)), 1 = Sin(t), 2 = Sin(Mul(2, t))

  SelfRewritingASTSource(int sampleRate, uint32_t seed = 0xA571'0001u)
  : sampleRate_(sampleRate), rng_(seed),
    amplitude_  (std::make_shared<ConstantSource>(0.5f)),
    freqScale_  (std::make_shared<ConstantSource>(float(2.0 * 3.14159265358979323846 * 220.0))),
    rewriteRate_(std::make_shared<ConstantSource>(4.0f)),
    smoothness_ (std::make_shared<ConstantSource>(0.0f))
  {
    clear_pool();
    reseed_tree();
  }

  const char* type_name() const override { return "SelfRewritingASTSource"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"amplitude",   0.5f,      0.0f,    10.0f},
      {"freqScale",   1382.3008f, 0.0f,   1.0e5f},   // ~2π*220
      {"rewriteRate", 4.0f,       0.0f,   1000.0f},
      {"smoothness",  0.0f,       0.0f,   0.999f},
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "amplitude")   { amplitude_   = std::move(src); return; }
    if (name == "freqScale")   { freqScale_   = std::move(src); return; }
    if (name == "rewriteRate") { rewriteRate_ = std::move(src); return; }
    if (name == "smoothness")  { smoothness_  = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "amplitude")   return amplitude_;
    if (name == "freqScale")   return freqScale_;
    if (name == "rewriteRate") return rewriteRate_;
    if (name == "smoothness")  return smoothness_;
    return nullptr;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"depthLimit", ConfigType::Int, 32.0f, 1.0f, float(kMaxDepth)},
      {"seedMode",   ConfigType::Int, 0.0f,  0.0f, 2.0f},
    };
    return descs;
  }

  void set_config(std::string_view name, float value) override {
    if (name == "depthLimit") { depthLimit = std::clamp(int(value), 1, kMaxDepth); return; }
    if (name == "seedMode")   {
      seedMode = std::clamp(int(value), 0, 2);
      clear_pool();
      reseed_tree();
      return;
    }
  }

  float get_config(std::string_view name) const override {
    if (name == "depthLimit") return float(depthLimit);
    if (name == "seedMode")   return float(seedMode);
    return 0.0f;
  }

  void prepare(int frames) override {
    if (amplitude_)   amplitude_->prepare(frames);
    if (freqScale_)   freqScale_->prepare(frames);
    if (rewriteRate_) rewriteRate_->prepare(frames);
    if (smoothness_)  smoothness_->prepare(frames);
  }

  float next() override {
    amplitude_->next();
    freqScale_->next();
    rewriteRate_->next();
    smoothness_->next();

    // Integrate time and rewrite credits.
    const double dt = double(freqScale_->current()) / double(sampleRate_);
    t_ += dt;
    if (t_ >  1.0e6) t_ -= 2.0e6;   // wrap rather than clamp to preserve phase continuity
    if (t_ < -1.0e6) t_ += 2.0e6;

    const float rr = std::max(0.0f, rewriteRate_->current());
    rewriteCredits_ += rr / float(sampleRate_);
    while (rewriteCredits_ >= 1.0f) {
      apply_rewrite();
      rewriteCredits_ -= 1.0f;
    }

    // Safety: if the root ever becomes invalid, reseed.
    if (rootIdx_ < 0 || rootIdx_ >= kPoolSize || !inUse_[rootIdx_]) {
      clear_pool();
      reseed_tree();
    }

    float raw = eval(rootIdx_, float(t_), 0);
    if (!std::isfinite(raw)) raw = 0.0f;
    raw = std::clamp(raw, -10.0f, 10.0f);

    const float s = std::clamp(smoothness_->current(), 0.0f, 0.999f);
    const float smoothed = (1.0f - s) * raw + s * smoothedPrev_;
    smoothedPrev_ = smoothed;

    cur_ = smoothed * amplitude_->current();
    return cur_;
  }

  float current() const override { return cur_; }

private:
  // -------------------------------------------------------------------------
  // Node model
  // -------------------------------------------------------------------------
  enum class NodeOp : uint8_t {
    Unused = 0,
    Const,
    TimeVar,
    Sin,
    Cos,
    Tanh,
    Add,
    Sub,
    Mul,
    Neg,
  };

  struct Node {
    NodeOp op{NodeOp::Unused};
    int16_t left{-1};
    int16_t right{-1};
    float value{0.0f};   // Const payload
  };

  static bool is_unary(NodeOp op) {
    return op == NodeOp::Sin || op == NodeOp::Cos || op == NodeOp::Tanh || op == NodeOp::Neg;
  }
  static bool is_binary(NodeOp op) {
    return op == NodeOp::Add || op == NodeOp::Sub || op == NodeOp::Mul;
  }

  // -------------------------------------------------------------------------
  // Pool management
  // -------------------------------------------------------------------------
  void clear_pool() {
    for (int i = 0; i < kPoolSize; ++i) {
      pool_[i] = Node{};
      inUse_[i] = false;
    }
    rootIdx_ = -1;
    nextFreeHint_ = 0;
  }

  // Scan from hint for first unused slot; returns -1 if the pool is full.
  int alloc_node(NodeOp op, int left, int right, float value) {
    for (int step = 0; step < kPoolSize; ++step) {
      int idx = (nextFreeHint_ + step) % kPoolSize;
      if (!inUse_[idx]) {
        Node& n = pool_[idx];
        n.op    = op;
        n.left  = int16_t(left);
        n.right = int16_t(right);
        n.value = value;
        inUse_[idx] = true;
        nextFreeHint_ = (idx + 1) % kPoolSize;
        return idx;
      }
    }
    return -1;
  }

  // Recursively free a subtree — each node's slot is marked free and cleared.
  void free_subtree(int idx) {
    if (idx < 0 || idx >= kPoolSize) return;
    if (!inUse_[idx]) return;
    Node n = pool_[idx];
    inUse_[idx] = false;
    pool_[idx]  = Node{};
    if (is_unary(n.op))  free_subtree(n.left);
    if (is_binary(n.op)) { free_subtree(n.left); free_subtree(n.right); }
  }

  int count_free() const {
    int c = 0;
    for (int i = 0; i < kPoolSize; ++i) if (!inUse_[i]) ++c;
    return c;
  }

  // -------------------------------------------------------------------------
  // Tree seeding
  // -------------------------------------------------------------------------
  void reseed_tree() {
    // Build the initial tree according to seedMode.
    switch (seedMode) {
      default:
      case 0: {
        // Mul(Const(0.5), Sin(TimeVar))
        int t     = alloc_node(NodeOp::TimeVar, -1, -1, 0.0f);
        int sinN  = alloc_node(NodeOp::Sin, t, -1, 0.0f);
        int halfN = alloc_node(NodeOp::Const, -1, -1, 0.5f);
        rootIdx_  = alloc_node(NodeOp::Mul, halfN, sinN, 0.0f);
        break;
      }
      case 1: {
        // Sin(TimeVar)
        int t    = alloc_node(NodeOp::TimeVar, -1, -1, 0.0f);
        rootIdx_ = alloc_node(NodeOp::Sin, t, -1, 0.0f);
        break;
      }
      case 2: {
        // Sin(Mul(Const(2), TimeVar))
        int t    = alloc_node(NodeOp::TimeVar, -1, -1, 0.0f);
        int two  = alloc_node(NodeOp::Const, -1, -1, 2.0f);
        int mulN = alloc_node(NodeOp::Mul, two, t, 0.0f);
        rootIdx_ = alloc_node(NodeOp::Sin, mulN, -1, 0.0f);
        break;
      }
    }
  }

  // -------------------------------------------------------------------------
  // Tree walking helpers
  // -------------------------------------------------------------------------
  // Count live nodes under (and including) idx.
  int count_subtree(int idx) const {
    if (idx < 0 || idx >= kPoolSize || !inUse_[idx]) return 0;
    const Node& n = pool_[idx];
    int c = 1;
    if (is_unary(n.op))  c += count_subtree(n.left);
    if (is_binary(n.op)) c += count_subtree(n.left) + count_subtree(n.right);
    return c;
  }

  // Walk the tree in pre-order picking the k-th node. parentIdx and whichChild
  // (0 = left, 1 = right, -1 = root) are written back for rewriting.
  // Returns selected node index, or -1 if out of range.
  int pick_kth(int idx, int& k, int parent, int whichChild,
               int& outParent, int& outWhich) const {
    if (idx < 0 || idx >= kPoolSize || !inUse_[idx]) return -1;
    if (k == 0) { outParent = parent; outWhich = whichChild; return idx; }
    --k;
    const Node& n = pool_[idx];
    if (is_unary(n.op)) {
      int r = pick_kth(n.left, k, idx, 0, outParent, outWhich);
      if (r >= 0) return r;
    } else if (is_binary(n.op)) {
      int r = pick_kth(n.left, k, idx, 0, outParent, outWhich);
      if (r >= 0) return r;
      r = pick_kth(n.right, k, idx, 1, outParent, outWhich);
      if (r >= 0) return r;
    }
    return -1;
  }

  // Replace a subtree pointer in parent (or root if parent < 0).
  void splice(int parent, int whichChild, int newIdx) {
    if (parent < 0) { rootIdx_ = newIdx; return; }
    if (whichChild == 0) pool_[parent].left  = int16_t(newIdx);
    else                 pool_[parent].right = int16_t(newIdx);
  }

  // Find a random Const node; returns -1 if none.
  int find_random_const() {
    int consts[kPoolSize];
    int n = 0;
    for (int i = 0; i < kPoolSize; ++i) {
      if (inUse_[i] && pool_[i].op == NodeOp::Const) consts[n++] = i;
    }
    if (n == 0) return -1;
    return consts[rng_.int_range(0, n - 1)];
  }

  // -------------------------------------------------------------------------
  // Rewrite rules — one picked uniformly per rewrite step.
  //
  //  0 Wrap in Sin        : N  → Sin(N)
  //  1 Wrap in Cos        : N  → Cos(N)
  //  2 Wrap in Tanh       : N  → Tanh(N)
  //  3 Scale              : N  → Mul(Const(r∈[0.3,2]), N)
  //  4 Harmonic add       : N  → Add(N, Mul(Const(r∈[0.2,0.7]),
  //                                         Sin(Mul(Const(2 or 3), TimeVar))))
  //  5 Replace Const      : pick Const node, set value to r∈[-2,2]
  //
  // If a rule would overflow the pool, fall back to rule 5 (no alloc).
  // If rule 5 has no Const, no-op.
  // -------------------------------------------------------------------------
  void apply_rewrite() {
    if (rootIdx_ < 0) return;
    int rule = rng_.int_range(0, 5);

    auto try_const_replace = [&]() {
      int c = find_random_const();
      if (c < 0) return;
      pool_[c].value = rng_.valuePN() * 2.0f;  // [-2, 2]
    };

    if (rule == 5) { try_const_replace(); return; }

    // Pick a target node uniformly by walking pre-order.
    int total = count_subtree(rootIdx_);
    if (total <= 0) return;
    int k = rng_.int_range(0, total - 1);
    int parent = -1, which = -1;
    int target = pick_kth(rootIdx_, k, -1, -1, parent, which);
    if (target < 0) return;

    // Each wrap rule needs 1 free slot; harmonic-add needs 6. If we can't
    // satisfy, fall back to const replace.
    const int free = count_free();

    switch (rule) {
      case 0:   // Sin
      case 1:   // Cos
      case 2: { // Tanh
        if (free < 1) { try_const_replace(); return; }
        NodeOp op = (rule == 0) ? NodeOp::Sin
                  : (rule == 1) ? NodeOp::Cos
                                : NodeOp::Tanh;
        int wrap = alloc_node(op, target, -1, 0.0f);
        if (wrap < 0) { try_const_replace(); return; }
        splice(parent, which, wrap);
        return;
      }
      case 3: { // Scale: Mul(Const(r), target)
        if (free < 2) { try_const_replace(); return; }
        float r = 0.3f + rng_.value() * (2.0f - 0.3f);
        int c   = alloc_node(NodeOp::Const, -1, -1, r);
        int mul = alloc_node(NodeOp::Mul, c, target, 0.0f);
        if (c < 0 || mul < 0) { try_const_replace(); return; }
        splice(parent, which, mul);
        return;
      }
      case 4: { // Harmonic add:
                // Add(target, Mul(Const(a), Sin(Mul(Const(h), TimeVar))))
        if (free < 6) { try_const_replace(); return; }
        float a = 0.2f + rng_.value() * (0.7f - 0.2f);
        float h = (rng_.int_range(0, 1) == 0) ? 2.0f : 3.0f;
        int tvar  = alloc_node(NodeOp::TimeVar, -1, -1, 0.0f);
        int hC    = alloc_node(NodeOp::Const, -1, -1, h);
        int mulHt = alloc_node(NodeOp::Mul, hC, tvar, 0.0f);
        int sinN  = alloc_node(NodeOp::Sin, mulHt, -1, 0.0f);
        int aC    = alloc_node(NodeOp::Const, -1, -1, a);
        int mulAS = alloc_node(NodeOp::Mul, aC, sinN, 0.0f);
        int addN  = alloc_node(NodeOp::Add, target, mulAS, 0.0f);
        if (tvar < 0 || hC < 0 || mulHt < 0 || sinN < 0
            || aC < 0 || mulAS < 0 || addN < 0) {
          try_const_replace();
          return;
        }
        splice(parent, which, addN);
        return;
      }
      default:
        try_const_replace();
        return;
    }
  }

  // -------------------------------------------------------------------------
  // Evaluation
  // -------------------------------------------------------------------------
  float eval(int idx, float t, int depth) const {
    if (depth > depthLimit) return 0.0f;
    if (idx < 0 || idx >= kPoolSize || !inUse_[idx]) return 0.0f;
    const Node& n = pool_[idx];
    switch (n.op) {
      case NodeOp::Const:   return n.value;
      case NodeOp::TimeVar: return t;
      case NodeOp::Sin:     return std::sin (eval(n.left,  t, depth + 1));
      case NodeOp::Cos:     return std::cos (eval(n.left,  t, depth + 1));
      case NodeOp::Tanh:    return std::tanh(eval(n.left,  t, depth + 1));
      case NodeOp::Neg:     return -eval(n.left, t, depth + 1);
      case NodeOp::Add:     return eval(n.left,  t, depth + 1) + eval(n.right, t, depth + 1);
      case NodeOp::Sub:     return eval(n.left,  t, depth + 1) - eval(n.right, t, depth + 1);
      case NodeOp::Mul:     return eval(n.left,  t, depth + 1) * eval(n.right, t, depth + 1);
      case NodeOp::Unused:
      default:              return 0.0f;
    }
  }

  // -------------------------------------------------------------------------
  // State
  // -------------------------------------------------------------------------
  int sampleRate_;
  Randomizer rng_;

  std::shared_ptr<ValueSource> amplitude_;
  std::shared_ptr<ValueSource> freqScale_;
  std::shared_ptr<ValueSource> rewriteRate_;
  std::shared_ptr<ValueSource> smoothness_;

  std::array<Node, kPoolSize>  pool_{};
  std::array<bool, kPoolSize>  inUse_{};
  int    rootIdx_{-1};
  int    nextFreeHint_{0};

  double t_{0.0};
  float  rewriteCredits_{0.0f};
  float  smoothedPrev_{0.0f};
  float  cur_{0.0f};
};

} // namespace mforce
