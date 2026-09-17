#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pierce_allpass.h"
#include <algorithm>
#include <cmath>
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
// EDGE MODES (the extension hook).  The edge filters are the one place the
// model's material character lives; they are the two `edge_x`/`edge_y` calls
// in tick0/tick1, and `edgeMode` selects which filter those calls run.  Both
// modes filter the same two faces (x = 0 and y = 0) and leave the far faces
// reflecting at unity, exactly as STK does, and both apply the `decay` pin to
// the filter input, so `decay` keeps its meaning as the per-hop edge loss.
//
//   0 "stk"   — STK's OnePole, pole 0.05, gain = decay.  DEFAULT, and
//               byte-identical to the validated port.
//   1 "chafe" — Chafe's 2nd-order allpass (ICSV26 2019, "Extensions to the
//               2D Waveguide Mesh for Modeling Thin Plate Vibrations"):
//                   H(z) = (a2 + a1 z^-1 + z^-2) / (1 + a1 z^-1 + a2 z^-2)
//                   a1 = -2 R cos(wc T),  a2 = R^2,  published fc = 1575 Hz,
//                   R = 0.75
//               One allpass per edge node, own state per node.  Unity
//               magnitude, phase only: it detunes/stretches the mode set
//               ("complex metallic timbres") without adding loss of its own.
//               fc and R come from the `edgeFc` / `edgeR` pins.
//   2 "pierce" — the Pierce/Van Duyne passive nonlinear filter (JASA 101(2)
//               1120-1126, 1997), one per edge node, which is the boundary
//               termination the patent itself names for a 2D mesh.  A
//               first-order allpass whose coefficient is a spring stiffness
//               switched by the sign of its own internal state: `coefNeg`
//               while that state is negative, `coefPos` while it is >= 0.
//               Equal coefficients = a linear allpass; the distance between
//               them is the nonlinearity.  See core/pierce_allpass.h and
//               docs/research/stk_port/PIERCE_PASSIVE_NOTES.md.
//
//               The reason this mode exists: mode 1's signal-dependent R (the
//               APPROXIMATION note below) is a TIME-VARYING allpass, which is
//               not passive — round 1 had to normalise the drive to half scale
//               and clamp R to stop three of four dynamic cells running away.
//               Mode 2 is passive per sample for every coefficient pair, so it
//               needs neither: full drive, no clamp, no runaway.  It is also a
//               genuinely PER-NODE nonlinearity — each edge node switches on
//               its own state — which is what Chafe's rule describes and what
//               the one-global-R stand-in could not do.
//
// APPROXIMATION, deliberate and worth knowing.  It applies to edgeMode 1 ONLY
// (mode 2 is per-node by construction).  Chafe's signal-dependent
// variants — r(n) = 0.75 + s*x(n), s = 0.2 for the "bashed aluminum pie pan",
// and Pierce differential stiffness s = -0.5 for x <= 0 / +0.003 for x > 0 for
// the gong-like modal upwelling — make R a function of the signal AT EACH EDGE
// NODE.  Here `edgeR` is a single pin shared by every allpass, so the dynamic
// behaviour is patched by wiring a tap of some mesh signal (normally the output
// tap) through a CurveNode into `edgeR`: one chosen node's displacement drives
// the whole boundary instead of each node driving its own.  That is a global
// approximation of a per-node rule, accepted for this round; per-node x would
// need the excitation-side signal broadcast into the node, not a pin.
struct Mesh2DSource final : ValueSource {
  static constexpr int kMaxN = 64;

  Mesh2DSource()
    : source_(std::make_shared<ConstantSource>(0.0f)),
      inX_(std::make_shared<ConstantSource>(0.0f)),
      inY_(std::make_shared<ConstantSource>(0.0f)),
      outX_(std::make_shared<ConstantSource>(1.0f)),
      outY_(std::make_shared<ConstantSource>(1.0f)),
      decay_(std::make_shared<ConstantSource>(0.99f)),
      edgeFc_(std::make_shared<ConstantSource>(kChafeFc)),
      edgeR_(std::make_shared<ConstantSource>(kChafeR)),
      coefNeg_(std::make_shared<ConstantSource>(kPierceNeg)),
      coefPos_(std::make_shared<ConstantSource>(kPiercePos)) {}

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
      // Read per sample in edgeMode 1 only; ignored in mode 0.
      {"edgeFc", kChafeFc, 0.0f, 20000.0f, "Hz"},
      {"edgeR",  kChafeR,  0.0f, kRMax,    "0-1"},
      // Read per sample in edgeMode 2 only; ignored in modes 0 and 1.
      {"coefNeg", kPierceNeg, -PierceAllpass::kCoefMax,
                              PierceAllpass::kCoefMax, "stiffness"},
      {"coefPos", kPiercePos, -PierceAllpass::kCoefMax,
                              PierceAllpass::kCoefMax, "stiffness"},
    };
    return descs;
  }

  std::span<const SettingDescriptor> setting_descriptors() const override {
    static constexpr const char* kEdgeModes[] = {"stk", "chafe", "pierce",
                                                 nullptr};
    static constexpr SettingDescriptor descs[] = {
      {"cols", SettingType::Int, 12.0f, 2.0f, float(kMaxN)},
      {"rows", SettingType::Int, 12.0f, 2.0f, float(kMaxN)},
      {"edgeMode", SettingType::Int, 0.0f, 0.0f, 2.0f, kEdgeModes},
    };
    return descs;
  }
  void set_setting(std::string_view name, float v) override {
    if (name == "edgeMode") { edgeMode_ = std::clamp(int(v), 0, 2); clear_mesh(); return; }
    const int n = std::clamp(int(v), 2, kMaxN);
    if      (name == "cols") { NX_ = n; clear_mesh(); }
    else if (name == "rows") { NY_ = n; clear_mesh(); }
  }
  float get_setting(std::string_view name) const override {
    if (name == "cols")     return float(NX_);
    if (name == "rows")     return float(NY_);
    if (name == "edgeMode") return float(edgeMode_);
    return 0.0f;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")     { source_ = std::move(src); return; }
    if (name == "inX")        { inX_    = std::move(src); return; }
    if (name == "inY")        { inY_    = std::move(src); return; }
    if (name == "outX")       { outX_   = std::move(src); return; }
    if (name == "outY")       { outY_   = std::move(src); return; }
    if (name == "decay")      { decay_  = std::move(src); return; }
    if (name == "edgeFc")     { edgeFc_ = std::move(src); return; }
    if (name == "edgeR")      { edgeR_  = std::move(src); return; }
    if (name == "coefNeg")    { coefNeg_ = std::move(src); return; }
    if (name == "coefPos")    { coefPos_ = std::move(src); return; }
  }
  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source") return source_;
    if (name == "inX")    return inX_;
    if (name == "inY")    return inY_;
    if (name == "outX")   return outX_;
    if (name == "outY")   return outY_;
    if (name == "decay")  return decay_;
    if (name == "edgeFc") return edgeFc_;
    if (name == "edgeR")  return edgeR_;
    if (name == "coefNeg") return coefNeg_;
    if (name == "coefPos") return coefPos_;
    return nullptr;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_) source_->prepare(ctx, frames);
    inX_->prepare(ctx, frames);
    inY_->prepare(ctx, frames);
    outX_->prepare(ctx, frames);
    outY_->prepare(ctx, frames);
    decay_->prepare(ctx, frames);
    edgeFc_->prepare(ctx, frames);
    edgeR_->prepare(ctx, frames);
    coefNeg_->prepare(ctx, frames);
    coefPos_->prepare(ctx, frames);
    sr_ = float(ctx.sampleRate > 0 ? ctx.sampleRate : 48000);
    clear_mesh();
    cur_ = 0.0f;
  }

  float next() override {
    const float in = source_ ? source_->next() : 0.0f;
    inX_->next(); inY_->next(); outX_->next(); outY_->next(); decay_->next();
    edgeFc_->next(); edgeR_->next(); coefNeg_->next(); coefPos_->next();
    const int xi = junction(inX_->current(),  NX_);
    const int yi = junction(inY_->current(),  NY_);
    const int xo = junction(outX_->current(), NX_);
    const int yo = junction(outY_->current(), NY_);
    gain_ = std::clamp(decay_->current(), 0.0f, 1.0f);

    if (edgeMode_ == 1) {
      // One cos() per sample for the whole boundary, not one per edge node:
      // edgeFc/edgeR are global pins, so all NX_+NY_ allpasses share these two
      // coefficients.  A trig call per sample is affordable at this node's
      // cost (the scattering loop is O(NX*NY) multiply-adds) and it is what
      // makes the pins continuously modulatable — which is the whole point of
      // the Chafe dynamic-R variants.
      const float R  = std::clamp(edgeR_->current(), 0.0f, kRMax);
      const float fc = std::clamp(edgeFc_->current(), 0.0f, 0.49f * sr_);
      a1_ = -2.0f * R * std::cos(6.28318530717958647692f * fc / sr_);
      a2_ = R * R;
    } else if (edgeMode_ == 2) {
      // Latched once per sample and shared by every edge node, like the mode
      // 1 coefficients — but unlike mode 1 the STATE that selects between
      // them is each node's own, so the nonlinearity is genuinely per-node.
      cNeg_ = PierceAllpass::clamp_coef(coefNeg_->current());
      cPos_ = PierceAllpass::clamp_coef(coefPos_->current());
    }

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
  static constexpr float kChafeFc = 1575.0f;       // Chafe 2019, published
  static constexpr float kChafeR  = 0.75f;         // Chafe 2019, published
  static constexpr float kRMax    = 0.999f;        // pole radius stability cap
  // edgeMode 2 defaults: Faust's own apnl example pair, +-0.5 — symmetric in
  // magnitude, so the energy rescale is exactly 1 there and the mode reduces
  // to the paper's literal recurrence.
  static constexpr float kPierceNeg =  0.5f;
  static constexpr float kPiercePos = -0.5f;

  // STK truncates the 0..1 factor to a junction index; see the header note.
  static int junction(float f, int n) {
    return std::clamp(int(std::clamp(f, 0.0f, 1.0f) * float(n - 1)), 0, n - 1);
  }

  // Chafe 2nd-order allpass, direct form I:
  //   y[n] = a2 x[n] + a1 x[n-1] + x[n-2] - a1 y[n-1] - a2 y[n-2]
  // `s` is one edge node's 4-float state (see the state layout below).
  float allpass(float* s, float x) {
    const float y = a2_ * x + a1_ * s[0] + s[1] - a1_ * s[2] - a2_ * s[3];
    s[1] = s[0]; s[0] = x;
    s[3] = s[2]; s[2] = y;
    return y;
  }

  float edge_x(int i, float x) {   // filterX_ — the y = 0 face
    if (edgeMode_ == 1) return allpass(fxAp_[i], gain_ * x);
    if (edgeMode_ == 2) return fxPc_[i].tick(gain_ * x, cNeg_, cPos_);
    const float y = kEdgeB0 * (gain_ * x) + kPole * fxState_[i];
    fxState_[i] = y;
    return y;
  }
  float edge_y(int i, float x) {   // filterY_ — the x = 0 face
    if (edgeMode_ == 1) return allpass(fyAp_[i], gain_ * x);
    if (edgeMode_ == 2) return fyPc_[i].tick(gain_ * x, cNeg_, cPos_);
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
    const float p0 = coefPos_ ? coefPos_->current() : kPiercePos;
    for (int i = 0; i < kMaxN; ++i) {
      fxState_[i] = fyState_[i] = 0.0f;
      for (int k = 0; k < 4; ++k) fxAp_[i][k] = fyAp_[i][k] = 0.0f;
      fxPc_[i].reset(p0);
      fyPc_[i].reset(p0);
    }
    counter_ = 0;
  }

  std::shared_ptr<ValueSource> source_, inX_, inY_, outX_, outY_, decay_,
      edgeFc_, edgeR_, coefNeg_, coefPos_;
  int NX_{12}, NY_{12};
  int edgeMode_{0};
  int counter_{0};
  float gain_{0.99f};
  float sr_{48000.0f};
  float a1_{0.0f}, a2_{0.0f};   // edgeMode 1 allpass coefficients, per sample
  float cNeg_{kPierceNeg}, cPos_{kPiercePos};  // edgeMode 2, per sample
  float cur_{0.0f};

  // Sized for the 64x64 cap at construction — set_setting/prepare never
  // resize, so next() cannot allocate.
  float v_[kMaxN - 1][kMaxN - 1]{};
  float vxp_[kMaxN][kMaxN]{}, vxm_[kMaxN][kMaxN]{};
  float vyp_[kMaxN][kMaxN]{}, vym_[kMaxN][kMaxN]{};
  float vxp1_[kMaxN][kMaxN]{}, vxm1_[kMaxN][kMaxN]{};
  float vyp1_[kMaxN][kMaxN]{}, vym1_[kMaxN][kMaxN]{};
  float fxState_[kMaxN]{}, fyState_[kMaxN]{};
  // edgeMode 1: one allpass per edge node, [x[n-1], x[n-2], y[n-1], y[n-2]].
  // Sized for the 64 cap like the mesh planes, so mode switching and
  // set_setting never allocate; clear_mesh() zeroes both modes' state.
  float fxAp_[kMaxN][4]{}, fyAp_[kMaxN][4]{};
  // edgeMode 2: one Pierce passive nonlinear allpass per edge node, each
  // switching on its OWN state.  Same 64-cap sizing rule as everything above.
  PierceAllpass fxPc_[kMaxN]{}, fyPc_[kMaxN]{};
};

} // namespace mforce
