#pragma once
#include "mforce/core/dsp_value_source.h"
#include <algorithm>
#include <memory>

namespace mforce {

// Rectilinear two-dimensional digital waveguide mesh — port of STK's Mesh2D
// (Van Duyne & Smith, "Physical Modeling with the 2-D Digital Waveguide
// Mesh", ICMC 1993; STK implementation by Julius Smith 2000-2002, revised by
// Gary Scavone 2002).  State layout, scattering order, alternating-buffer
// scheme and edge handling are STK's verbatim; see the deviations below.
//
// Per sample (STK Mesh2D::inputTick): the excitation is added to the two
// positive-going waves at the input junction, then one of two mirror-image
// half-steps runs (tick0 reads the unprimed planes and writes the primed
// ones, tick1 the reverse).  Each half-step is: (1) junction velocities
// v = (vxp + vxm + vyp + vym) * 0.5, (2) outgoing waves v - incoming, written
// into the alternate planes, (3) edge reflection — filtered on the x=0 and
// y=0 faces, free (unit reflection) on the far faces, (4) output tap read
// from the pre-step positive-going planes.
//
// SAMPLE RATE: the mesh carries no rate-baked filter design.  Propagation is
// exactly one sample per hop, so the geometry is specified in samples and the
// model is scale-free: N = SR * len / c (Chafe eq. 1) maps a physical plate
// onto a mesh, and holding N fixed while changing SR simply rescales every
// modal frequency.  The two edge-filter constants (pole 0.05, gain = decay)
// are per-hop, i.e. normalized-frequency, quantities — they do not need the
// re-realization the rate-discipline lesson demands of STK's fixed-Hz
// coefficients (STK_PORT_NOTES.md, "THE 48k BASELINE BUG").  Validated at
// 48000 against STK, which is also Chafe's own rate.
//
// DEVIATIONS FROM STK, both additive (corner output + decay 0.99 reproduce
// STK bit-for-bit):
//   * Output position.  STK hard-codes the tap at the far corner,
//     vxp[NX-1][NY-2] + vyp[NX-2][NY-1].  Here the tap moves: with the output
//     junction at (ox, oy) it reads vxp[ox][oy-1] + vyp[ox-1][oy], which is
//     STK's expression exactly when (ox, oy) = (NX-1, NY-1).
//   * Mesh cap.  STK's NXMAX/NYMAX are 12; here 64, so Chafe-sized plates fit.
//   * decay pin.  STK's setDecay (CC 11) as a per-sample pin.
// NOT a deviation, and deliberately not "fixed": STK truncates the position
// factors to integer junctions (xInput = (unsigned short)(xFactor*(NX-1))),
// so a swept position steps rather than glides.  Interpolated (Chafe-style
// moving) taps are an extension, not part of this port.
//
// EXTENSION HOOK: the edge filters are the one place the model's material
// character lives.  They are the two `edge_x`/`edge_y` calls in tick0/tick1;
// a richer boundary (per-face filters, frequency-dependent or anisotropic
// loss) replaces those calls and nothing else.
struct Mesh2DSource final : ValueSource {
  static constexpr int kMaxN = 64;

  Mesh2DSource()
    : source_(std::make_shared<ConstantSource>(0.0f)),
      inX_(std::make_shared<ConstantSource>(0.0f)),
      inY_(std::make_shared<ConstantSource>(0.0f)),
      outX_(std::make_shared<ConstantSource>(1.0f)),
      outY_(std::make_shared<ConstantSource>(1.0f)),
      decay_(std::make_shared<ConstantSource>(0.99f)) {}

  const char* type_name() const override { return "Mesh2D"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
    };
    return descs;
  }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"inX",   0.0f, 0.0f, 1.0f, "0-1"},
      {"inY",   0.0f, 0.0f, 1.0f, "0-1"},
      {"outX",  1.0f, 0.0f, 1.0f, "0-1"},
      {"outY",  1.0f, 0.0f, 1.0f, "0-1"},
      {"decay", 0.99f, 0.0f, 1.0f, "0-1"},
    };
    return descs;
  }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr SettingDescriptor descs[] = {
      {"cols", SettingType::Int, 12.0f, 2.0f, float(kMaxN)},
      {"rows", SettingType::Int, 12.0f, 2.0f, float(kMaxN)},
    };
    return descs;
  }
  void set_setting(std::string_view name, float v) override {
    const int n = std::clamp(int(v), 2, kMaxN);
    if      (name == "cols") { NX_ = n; clear_mesh(); }
    else if (name == "rows") { NY_ = n; clear_mesh(); }
  }
  float get_setting(std::string_view name) const override {
    if (name == "cols") return float(NX_);
    if (name == "rows") return float(NY_);
    return 0.0f;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "inX")        { inX_    = std::move(src); return; }
    if (name == "inY")        { inY_    = std::move(src); return; }
    if (name == "outX")       { outX_   = std::move(src); return; }
    if (name == "outY")       { outY_   = std::move(src); return; }
    if (name == "decay")      { decay_  = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source") return source_;
    if (name == "inX")    return inX_;
    if (name == "inY")    return inY_;
    if (name == "outX")   return outX_;
    if (name == "outY")   return outY_;
    if (name == "decay")  return decay_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    inX_->prepare(ctx, frames);
    inY_->prepare(ctx, frames);
    outX_->prepare(ctx, frames);
    outY_->prepare(ctx, frames);
    decay_->prepare(ctx, frames);
    clear_mesh();
    cur_ = 0.0f;
  }

  float next() override {
    const float in = source_ ? source_->next() : 0.0f;
    inX_->next(); inY_->next(); outX_->next(); outY_->next(); decay_->next();
    const int xi = junction(inX_->current(),  NX_);
    const int yi = junction(inY_->current(),  NY_);
    const int xo = junction(outX_->current(), NX_);
    const int yo = junction(outY_->current(), NY_);
    gain_ = std::clamp(decay_->current(), 0.0f, 1.0f);

    if (counter_ & 1) {
      vxp1_[xi][yi] += in;
      vyp1_[xi][yi] += in;
      cur_ = tick1(xo, yo);
    } else {
      vxp_[xi][yi] += in;
      vyp_[xi][yi] += in;
      cur_ = tick0(xo, yo);
    }
    ++counter_;
    return cur_;
  }
  float current() const override { return cur_; }

private:
  static constexpr float kVScale = 0.5f;
  static constexpr float kPole   = 0.05f;          // STK Mesh2D ctor
  static constexpr float kEdgeB0 = 1.0f - kPole;   // OnePole::setPole

  // STK truncates the 0..1 factor to a junction index; see the header note.
  static int junction(float f, int n) {
    return std::clamp(int(std::clamp(f, 0.0f, 1.0f) * float(n - 1)), 0, n - 1);
  }

  float edge_x(int i, float x) {   // filterX_ — the y = 0 face
    const float y = kEdgeB0 * (gain_ * x) + kPole * fxState_[i];
    fxState_[i] = y;
    return y;
  }
  float edge_y(int i, float x) {   // filterY_ — the x = 0 face
    const float y = kEdgeB0 * (gain_ * x) + kPole * fyState_[i];
    fyState_[i] = y;
    return y;
  }

  float tick0(int xo, int yo) {
    for (int x = 0; x < NX_ - 1; ++x)
      for (int y = 0; y < NY_ - 1; ++y)
        v_[x][y] = (vxp_[x][y] + vxm_[x + 1][y] +
                    vyp_[x][y] + vym_[x][y + 1]) * kVScale;

    for (int x = 0; x < NX_ - 1; ++x) {
      for (int y = 0; y < NY_ - 1; ++y) {
        const float vxy = v_[x][y];
        vxp1_[x + 1][y] = vxy - vxm_[x + 1][y];
        vyp1_[x][y + 1] = vxy - vym_[x][y + 1];
        vxm1_[x][y]     = vxy - vxp_[x][y];
        vym1_[x][y]     = vxy - vyp_[x][y];
      }
    }

    for (int y = 0; y < NY_ - 1; ++y) {
      vxp1_[0][y]        = edge_y(y, vxm_[0][y]);
      vxm1_[NX_ - 1][y]  = vxp_[NX_ - 1][y];
    }
    for (int x = 0; x < NX_ - 1; ++x) {
      vyp1_[x][0]        = edge_x(x, vym_[x][0]);
      vym1_[x][NY_ - 1]  = vyp_[x][NY_ - 1];
    }

    return vxp_[xo][yo > 0 ? yo - 1 : 0] + vyp_[xo > 0 ? xo - 1 : 0][yo];
  }

  float tick1(int xo, int yo) {
    for (int x = 0; x < NX_ - 1; ++x)
      for (int y = 0; y < NY_ - 1; ++y)
        v_[x][y] = (vxp1_[x][y] + vxm1_[x + 1][y] +
                    vyp1_[x][y] + vym1_[x][y + 1]) * kVScale;

    for (int x = 0; x < NX_ - 1; ++x) {
      for (int y = 0; y < NY_ - 1; ++y) {
        const float vxy = v_[x][y];
        vxp_[x + 1][y] = vxy - vxm1_[x + 1][y];
        vyp_[x][y + 1] = vxy - vym1_[x][y + 1];
        vxm_[x][y]     = vxy - vxp1_[x][y];
        vym_[x][y]     = vxy - vyp1_[x][y];
      }
    }

    for (int y = 0; y < NY_ - 1; ++y) {
      vxp_[0][y]        = edge_y(y, vxm1_[0][y]);
      vxm_[NX_ - 1][y]  = vxp1_[NX_ - 1][y];
    }
    for (int x = 0; x < NX_ - 1; ++x) {
      vyp_[x][0]        = edge_x(x, vym1_[x][0]);
      vym_[x][NY_ - 1]  = vyp1_[x][NY_ - 1];
    }

    return vxp1_[xo][yo > 0 ? yo - 1 : 0] + vyp1_[xo > 0 ? xo - 1 : 0][yo];
  }

  void clear_mesh() {
    for (int x = 0; x < kMaxN; ++x) {
      for (int y = 0; y < kMaxN; ++y) {
        vxp_[x][y] = vxm_[x][y] = vyp_[x][y] = vym_[x][y] = 0.0f;
        vxp1_[x][y] = vxm1_[x][y] = vyp1_[x][y] = vym1_[x][y] = 0.0f;
        if (x < kMaxN - 1 && y < kMaxN - 1) v_[x][y] = 0.0f;
      }
    }
    for (int i = 0; i < kMaxN; ++i) fxState_[i] = fyState_[i] = 0.0f;
    counter_ = 0;
  }

  std::shared_ptr<ValueSource> source_, inX_, inY_, outX_, outY_, decay_;
  int NX_{12}, NY_{12};
  int counter_{0};
  float gain_{0.99f};
  float cur_{0.0f};

  // Sized for the 64x64 cap at construction — set_setting/prepare never
  // resize, so next() cannot allocate.
  float v_[kMaxN - 1][kMaxN - 1]{};
  float vxp_[kMaxN][kMaxN]{}, vxm_[kMaxN][kMaxN]{};
  float vyp_[kMaxN][kMaxN]{}, vym_[kMaxN][kMaxN]{};
  float vxp1_[kMaxN][kMaxN]{}, vxm1_[kMaxN][kMaxN]{};
  float vyp1_[kMaxN][kMaxN]{}, vym1_[kMaxN][kMaxN]{};
  float fxState_[kMaxN]{}, fyState_[kMaxN]{};
};

} // namespace mforce
