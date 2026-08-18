#pragma once
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/randomizer.h"
#include <memory>
#include <vector>
#include <cmath>
#include <algorithm>

namespace mforce {

// ---------------------------------------------------------------------------
// KSPianoString — Karplus-Strong-family piano string block (dsp run 24,
// port of Balazs Gyutai's Alpha Forever piano description).
//
// One node encapsulates everything in the description that forms feedback
// loops — the ValueSource graph is acyclic, so the loops must live INSIDE a
// node, not as graph edges:
//
//   1. Inharmonics generator: a feedback comb loop whose feedback path is a
//      one-pole highpass followed by 3-times NESTED first-order allpasses
//      (allpass inside an allpass inside an allpass, Schroeder/Gardner
//      lattice with unit inner delays). "Important for lower notes" —
//      controlled by inharmGain, per-note mappable.
//   2. Main resonators: numCombs (<=3) feedback comb filters, detuned in
//      unison fashion (detune cents spread -> the realistic flanging), each
//      loop containing 2 second-order allpasses (double-real-pole biquads,
//      coefficient = dispersion) for frequency-dependent delay = stretched
//      partials, plus a one-pole lowpass damping (brightness) so high
//      partials decay faster. Loop gain is derived from t60 at f0.
//   3. A small NEGATIVE feedback loop around (inharmonics + resonators)
//      (fbCoeff) — the double-envelope mechanism of the description.
//      fbCoeff is normalized by the comb resonance headroom: the applied
//      coefficient is fbCoeff * (1 - loopGain), because the comb peak gain
//      is ~1/(1 - loopGain) (thousands at long t60) and a raw coefficient
//      goes unstable the moment t60 rises. fbCoeff ~= open-loop gain at the
//      resonance peaks; keep it well under 1.
//
// The excitation input (`source`) is the hammer chain output — per the
// description a decaying envelope, optionally shaped by HammerBank. A DC
// blocker sits on the excitation input (the raw envelope is unipolar; DC in
// a comb loop otherwise rings at the k=0 resonance as a thump).
//
// Tuning: comb delay = period - phase delay of the in-loop filters at f0
// (computed analytically at note init), fractional part via linear-interp
// read. Frequency is captured at the first next() after prepare() (per-note
// lazy init, same convention as WavetableSource); paramMap "frequency"
// retunes, and paramMap frequency->curve entries can drive the scalar
// configs (dispersion, t60, brightness, inharmGain, ...) per note.
//
// Real-time safe: delay buffers allocated once at construction, sized for
// ~12 Hz at 48k (kBufLen); prepare() only zero-fills; next() allocates
// nothing.
// ---------------------------------------------------------------------------
struct KSPianoString final : ValueSource {

  explicit KSPianoString(int sampleRate)
  : sampleRate_(sampleRate) {
    frequency_ = std::make_shared<ConstantSource>(220.0f);
    amplitude_ = std::make_shared<ConstantSource>(1.0f);
    for (int i = 0; i < kMaxCombs; ++i) comb_[i].buf.assign(kBufLen, 0.0f);
    disp_.buf.assign(kBufLen, 0.0f);
  }

  const char* type_name() const override { return "KSPianoString"; }
  SourceCategory category() const override { return SourceCategory::Oscillator; }

  std::span<const ParamDescriptor> param_descriptors() const override {
    static constexpr ParamDescriptor descs[] = {
      {"frequency", 220.0f, 12.0f, 8000.0f, "hz"},
      {"amplitude", 1.0f,   0.0f,  10.0f,   "0-1"},
    };
    return descs;
  }

  std::span<const InputDescriptor> input_descriptors() const override {
    static constexpr InputDescriptor descs[] = {
      {"source"},
      {"damper"},
    };
    return descs;
  }

  std::span<const ConfigDescriptor> config_descriptors() const override {
    static constexpr ConfigDescriptor descs[] = {
      {"numCombs",   ConfigType::Int,   3.0f,    1.0f,   3.0f},
      {"detune",     ConfigType::Float, 0.75f,   0.0f,   7200.0f}, // cents PER SIDE, AF semantics (outer combs at +/-detune; 600 = tritones, 1200 = octaves). 2026-08-18: was total-spread — all stored patches halved to compensate, bit-exact.
      {"t60",        ConfigType::Float, 6.0f,    0.05f,  60.0f},   // sec at f0
      {"brightness", ConfigType::Float, 0.6f,    0.05f,  1.0f},    // loop LP coeff
      {"dispersion", ConfigType::Float, 0.12f,   0.0f,   0.95f},   // biquad AP pole
      {"inharmGain", ConfigType::Float, 0.25f,   0.0f,   4.0f},
      {"inharmFb",   ConfigType::Float, 0.90f,   0.0f,   0.995f},
      {"inharmHp",   ConfigType::Float, 150.0f,  10.0f,  5000.0f}, // hz
      {"ap1",        ConfigType::Float, 0.55f,  -0.95f,  0.95f},
      {"ap2",        ConfigType::Float, 0.35f,  -0.95f,  0.95f},
      {"ap3",        ConfigType::Float, 0.20f,  -0.95f,  0.95f},
      {"fbCoeff",    ConfigType::Float, 0.2f,    0.0f,   0.9f},    // global neg fb (headroom-normalized)
      {"releaseFb",  ConfigType::Float, 1.0f,    0.0f,   1.0f},    // loop-gain mult after note-off (damper; 1 = off)
      {"damperNoise", ConfigType::Float, 0.0f,   0.0f,   2.0f},    // felt-contact noise at note-off, scaled by ring level
      {"exciteGain", ConfigType::Float, 1.0f,    0.0f,   8.0f},
      {"direct",     ConfigType::Float, 0.2f,    0.0f,   1.0f},    // dry strike tap
    };
    return descs;
  }

  void set_param(std::string_view name, std::shared_ptr<ValueSource> src) override {
    if (name == "source")    { source_    = std::move(src); return; }
    if (name == "damper")    { damper_    = std::move(src); return; }
    if (name == "frequency") { frequency_ = std::move(src); return; }
    if (name == "amplitude") { amplitude_ = std::move(src); return; }
  }

  std::shared_ptr<ValueSource> get_param(std::string_view name) const override {
    if (name == "source")    return source_;
    if (name == "damper")    return damper_;
    if (name == "frequency") return frequency_;
    if (name == "amplitude") return amplitude_;
    return nullptr;
  }

  void set_config(std::string_view name, float v) override {
    if (name == "numCombs")   { numCombs_   = std::clamp(int(v), 1, kMaxCombs); return; }
    if (name == "detune")     { detune_     = v; return; }
    if (name == "t60")        { t60_        = std::max(v, 0.05f); return; }
    if (name == "brightness") { brightness_ = std::clamp(v, 0.05f, 1.0f); return; }
    if (name == "dispersion") { dispersion_ = std::clamp(v, 0.0f, 0.95f); return; }
    if (name == "inharmGain") { inharmGain_ = v; return; }
    if (name == "inharmFb")   { inharmFb_   = std::clamp(v, 0.0f, 0.995f); return; }
    if (name == "inharmHp")   { inharmHp_   = v; return; }
    if (name == "ap1")        { ap_[0]      = std::clamp(v, -0.95f, 0.95f); return; }
    if (name == "ap2")        { ap_[1]      = std::clamp(v, -0.95f, 0.95f); return; }
    if (name == "ap3")        { ap_[2]      = std::clamp(v, -0.95f, 0.95f); return; }
    if (name == "fbCoeff")    { fbCoeff_    = v; return; }
    if (name == "releaseFb")  { releaseFb_  = std::clamp(v, 0.0f, 1.0f); return; }
    if (name == "damperNoise") { damperNoise_ = std::clamp(v, 0.0f, 2.0f); return; }
    if (name == "exciteGain") { exciteGain_ = v; return; }
    if (name == "direct")     { direct_     = v; return; }
  }

  float get_config(std::string_view name) const override {
    if (name == "numCombs")   return float(numCombs_);
    if (name == "detune")     return detune_;
    if (name == "t60")        return t60_;
    if (name == "brightness") return brightness_;
    if (name == "dispersion") return dispersion_;
    if (name == "inharmGain") return inharmGain_;
    if (name == "inharmFb")   return inharmFb_;
    if (name == "inharmHp")   return inharmHp_;
    if (name == "ap1")        return ap_[0];
    if (name == "ap2")        return ap_[1];
    if (name == "ap3")        return ap_[2];
    if (name == "fbCoeff")    return fbCoeff_;
    if (name == "releaseFb")  return releaseFb_;
    if (name == "damperNoise") return damperNoise_;
    if (name == "exciteGain") return exciteGain_;
    if (name == "direct")     return direct_;
    return 0.0f;
  }

  void prepare(const RenderContext& ctx, int frames) override {
    if (source_)    source_->prepare(ctx, frames);
    if (damper_)    damper_->prepare(ctx, frames);
    if (frequency_) frequency_->prepare(ctx, frames);
    if (amplitude_) amplitude_->prepare(ctx, frames);

    for (int i = 0; i < kMaxCombs; ++i) {
      std::fill(comb_[i].buf.begin(), comb_[i].buf.end(), 0.0f);
      comb_[i].lp = 0.0f;
      comb_[i].apX = comb_[i].apY = 0.0f;
      for (int b = 0; b < 2; ++b) comb_[i].bq[b] = BiquadState{};
    }
    std::fill(disp_.buf.begin(), disp_.buf.end(), 0.0f);
    disp_.d1 = disp_.d2 = disp_.d3 = 0.0f;
    disp_.hpY = disp_.hpX = 0.0f;
    wpos_ = 0;
    dcX_ = dcY_ = 0.0f;
    lastOut_ = 0.0f;
    initialized_ = false;
    cur_ = 0.0f;
    lastD_ = 0.0f;
    envFollow_ = 0.0f;
    dnAmp_ = 0.0f;
  }

  float next() override {
    if (frequency_) frequency_->next();
    if (amplitude_) amplitude_->next();
    float exc = 0.0f;
    if (source_) { source_->next(); exc = source_->current(); }

    if (!initialized_) init_note();

    // In-loop damper, now a continuous input (0 = open, 1 = fully damped).
    // d scales the loop gain toward releaseFb — at 1 identical to the old
    // post-note-off choke, in between it's a partially lifted damper
    // (half-pedaling). Driven by a damper-preset Envelope whose final
    // stage IS the note's release phase (note-contained sound, 2026-08-13).
    float d = 0.0f;
    if (damper_) { damper_->next(); d = std::clamp(damper_->current(), 0.0f, 1.0f); }
    float damp = 1.0f - d * (1.0f - releaseFb_);
    // Damper-contact noise: each time the felt LANDS (rising threshold
    // crossing), capture the ring level and ring a short burst through the
    // now-damped string — injected into the loop input below, so the burst
    // excites the damped string rather than sitting on top. Re-arms when
    // the damper lifts: a re-dropped damper on a still-ringing string thuds
    // again, scaled by whatever ring is left.
    float dnoise = 0.0f;
    if (damperNoise_ > 0.0001f) {
      if (d >= 0.05f && lastD_ < 0.05f) dnAmp_ = damperNoise_ * envFollow_;
      // Contact noise exists only while the felt is touching the string —
      // without the d-gate, a burst not yet decayed out kept injecting
      // into the RE-OPENED string after the damper env ended (live
      // staccato: seconds of noise-driven ring, 2026-08-13).
      if (dnAmp_ > 1e-6f && d > 0.02f) {
        dnoise = dnRng_.valuePN() * dnAmp_;
        dnAmp_ *= 0.9995f;   // -60 dB over ~0.29 s at 48 kHz
      }
    }
    lastD_ = d;

    // DC blocker on excitation (envelope input is unipolar)
    float x = dcR_ * dcY_ + exc - dcX_;
    dcX_ = exc; dcY_ = x;
    x *= exciteGain_;

    // Global negative feedback around inharmonics + resonators
    float xin = x - fbScale_ * lastOut_;

    // ---- Inharmonics: nested-allpass HP-filtered feedback comb ----
    float inh = 0.0f;
    if (inharmGain_ > 0.0001f) {
      int rd = wpos_ - dispLen_;
      if (rd < 0) rd += kBufLen;
      float d = disp_.buf[rd];
      // 3-times nested first-order allpasses (Schroeder lattice, unit inner
      // delays; each inner network sits in the delay branch of its outer).
      // State: d1 = delayed v1 (outer), d2 = delayed v2 (middle),
      // d3 = delayed v3 (innermost). Each old state is consumed before its
      // slot is overwritten, so no temporaries are needed.
      float t3 = disp_.d3;
      float v3 = disp_.d2 + ap_[2] * t3;          // innermost: input = delayed v2
      float y3 = -ap_[2] * v3 + t3;
      disp_.d3 = v3;
      float v2 = disp_.d1 + ap_[1] * y3;          // middle: input = delayed v1
      float y2 = -ap_[1] * v2 + y3;
      disp_.d2 = v2;
      float v1 = d + ap_[0] * y2;                  // outer: input = comb read
      float y1 = -ap_[0] * v1 + y2;
      disp_.d1 = v1;
      // One-pole highpass in the loop
      float hp = hpR_ * (disp_.hpY + y1 - disp_.hpX);
      disp_.hpX = y1; disp_.hpY = hp;
      disp_.buf[wpos_] = xin + inharmFb_ * damp * hp;
      inh = d;
    } else {
      disp_.buf[wpos_] = 0.0f;
    }

    // ---- Main resonators: detuned combs with biquad allpasses ----
    float s = xin + inharmGain_ * inh + dnoise;
    float sum = 0.0f;
    for (int i = 0; i < numCombs_; ++i) {
      Comb& c = comb_[i];
      float r;
      if (c.apRead) {
        // Allpass fractional read: integer tap + first-order allpass
        // H(z) = (eta + z^-1)/(1 + eta z^-1) — lossless at all
        // frequencies (see init_note).
        int rd = wpos_ - c.lenInt;
        if (rd < 0) rd += kBufLen;
        float xr = c.buf[rd];
        r = c.apEta * (xr - c.apY) + c.apX;
        c.apX = xr; c.apY = r;
      } else {
        // Fractional read Li samples back (linear interp)
        float rp = float(wpos_) - c.len;
        if (rp < 0.0f) rp += float(kBufLen);
        int i0 = int(rp);
        int i1 = i0 + 1; if (i1 >= kBufLen) i1 = 0;
        float frac = rp - float(i0);
        r = c.buf[i0] + (c.buf[i1] - c.buf[i0]) * frac;
      }

      // Loop path: 2 biquad allpasses (dispersion) -> damping LP -> gain
      float v = r;
      if (dispActive_) {
        v = biquad_ap(v, c.bq[0]);
        v = biquad_ap(v, c.bq[1]);
      }
      c.lp += brightness_ * (v - c.lp);
      c.buf[wpos_] = s + c.gain * damp * c.lp;

      sum += r;
    }
    float combMix = sum / float(numCombs_);
    lastOut_ = combMix;
    // Ring-level follower for the damper-contact noise (fast attack, slow
    // decay; frozen into dnAmp_ at note-off).
    float acm = std::fabs(combMix);
    envFollow_ = acm > envFollow_ ? acm : envFollow_ * 0.99995f;

    wpos_ = (wpos_ + 1) % kBufLen;

    float ampl = amplitude_ ? amplitude_->current() : 1.0f;
    cur_ = (combMix + direct_ * s) * ampl;
    return cur_;
  }

  float current() const override { return cur_; }

private:
  static constexpr int kMaxCombs = 3;
  static constexpr int kBufLen   = 4096;  // >= sr/12Hz at 48k
  // Minimum comb delay the dispersion-shedding guard preserves (samples).
  static constexpr float kMinCombLen = 4.0f;
  // Above this angular frequency the comb uses the allpass fractional
  // read (linear-interp loss becomes material); below, linear interp is
  // kept so the approved low/mid keyboard is bit-identical.
  static constexpr float kApReadW0 = 0.1f;   // ~764 Hz at 48 kHz

  struct BiquadState { float x1{0}, x2{0}, y1{0}, y2{0}; };

  float releaseFb_{1.0f};
  float damperNoise_{0.0f};
  float lastD_{0.0f};
  float envFollow_{0.0f};
  float dnAmp_{0.0f};
  Randomizer dnRng_{0xDA3B0E5u};

  struct Comb {
    std::vector<float> buf;
    float len{100.0f};   // fractional loop delay (samples)
    float gain{0.99f};   // loop gain from t60
    float lp{0.0f};      // damping one-pole state
    // Allpass fractional read (top of the keyboard; see init_note)
    bool  apRead{false};
    int   lenInt{100};
    float apEta{0.0f};
    float apX{0.0f}, apY{0.0f};
    BiquadState bq[2];
  };

  struct DispLoop {
    std::vector<float> buf;
    float d1{0}, d2{0}, d3{0};   // nested allpass unit delays
    float hpX{0}, hpY{0};
  };

  // Double-real-pole allpass biquad: H(z) = (a^2 - 2a z^-1 + z^-2) /
  // (1 - 2a z^-1 + a^2 z^-2), a = dispersion. Group delay decreases with
  // frequency -> upper partials see a shorter loop -> stretched (sharp)
  // partials, the KS route to piano inharmonicity.
  float biquad_ap(float x, BiquadState& st) const {
    const float a = dispEff_;
    float y = a * a * x - 2.0f * a * st.x1 + st.x2
            + 2.0f * a * st.y1 - a * a * st.y2;
    st.x2 = st.x1; st.x1 = x;
    st.y2 = st.y1; st.y1 = y;
    return y;
  }

  // Phase delay (samples) of the first-order allpass (z^-1 - a)/(1 - a z^-1)
  // at angular frequency w. Used at note init to tune the comb length so the
  // fundamental lands on pitch despite the in-loop filters.
  static float ap1_phase_delay(float a, float w) {
    float cw = std::cos(w), sw = std::sin(w);
    float numRe = cw - a,          numIm = -sw;
    float denRe = 1.0f - a * cw,   denIm = a * sw;
    float ph = std::atan2(numIm, numRe) - std::atan2(denIm, denRe);
    return (w > 1e-6f) ? -ph / w : (1.0f + a) / (1.0f - a);
  }

  // Phase delay (samples) of the fractional-delay allpass
  // H(z) = (eta + z^-1)/(1 + eta z^-1) at angular frequency w. DC limit
  // (1-eta)/(1+eta) = the nominal fractional delay for eta=(1-d)/(1+d).
  static float apfrac_phase_delay(float eta, float w) {
    float cw = std::cos(w), sw = std::sin(w);
    float numRe = eta + cw,        numIm = -sw;
    float denRe = 1.0f + eta * cw, denIm = -eta * sw;
    float ph = std::atan2(numIm, numRe) - std::atan2(denIm, denRe);
    return (w > 1e-6f) ? -ph / w : (1.0f - eta) / (1.0f + eta);
  }

  void init_note() {
    float f0 = frequency_ ? frequency_->current() : 220.0f;
    f0 = std::clamp(f0, 12.0f, float(sampleRate_) * 0.4f);
    const float sr = float(sampleRate_);
    const float period = sr / f0;
    const float w0 = 2.0f * 3.14159265f * f0 / sr;

    dispActive_ = dispersion_ > 0.001f;
    dispEff_ = dispersion_;

    // Phase delay of the loop filters at f0:
    // 2 biquads = 4 first-order sections at pole `dispersion`
    float apDelay = dispActive_ ? 4.0f * ap1_phase_delay(dispEff_, w0) : 0.0f;
    // Comb-floor guard (2026-08-13): at the top of the keyboard the
    // dispersion chain's phase delay (~10 samples at a=0.5) can exceed
    // what the period leaves for the comb (B8 period = 12.15 at 48 kHz),
    // so len used to clamp at the floor — up to -40 cents mistuning,
    // per-note ring/dead chaos across octave 8, and a self-oscillating
    // B8. Instead of clamping, SHED dispersion per note: bisect the
    // largest pole whose phase delay still leaves kMinCombLen samples of
    // comb at the highest-detuned comb. Physically honest — top-octave
    // strings are effectively dispersion-free (stretched partials sit
    // above Nyquist). Notes with room to spare are untouched.
    {
      const float maxDet = std::pow(2.0f, detune_ / 1200.0f);
      const float minPeriod = period / maxDet;
      if (dispActive_ && minPeriod - apDelay - lpDelay_ < kMinCombLen) {
        float lo = 0.0f, hi = dispEff_;
        for (int it = 0; it < 24; ++it) {
          float mid = 0.5f * (lo + hi);
          float d = 4.0f * ap1_phase_delay(mid, w0);
          if (minPeriod - d - lpDelay_ < kMinCombLen) hi = mid; else lo = mid;
        }
        dispEff_ = lo;
        dispActive_ = dispEff_ > 0.001f;
        apDelay = dispActive_ ? 4.0f * ap1_phase_delay(dispEff_, w0) : 0.0f;
      }
    }
    // damping one-pole y += c(x-y): H = c / (1-(1-c)z^-1); phase delay at w0
    {
      float b = 1.0f - brightness_;
      float cw = std::cos(w0), sw = std::sin(w0);
      float argH = -std::atan2(b * sw, 1.0f - b * cw);
      lpDelay_ = -argH / w0;
    }

    // Unison detune offsets (cents): the flanging mechanism of the
    // description — detuning detunes the comb lengths together.
    float off[kMaxCombs] = {0.0f, 0.0f, 0.0f};
    if (numCombs_ == 2)      { off[0] = -1.0f; off[1] = 1.0f; }
    else if (numCombs_ == 3) { off[0] = -1.0f; off[1] = 0.0f; off[2] = 1.0f; }

    for (int i = 0; i < numCombs_; ++i) {
      float det = std::pow(2.0f, (off[i] * detune_) / 1200.0f);
      float len = period / det - apDelay - lpDelay_;
      comb_[i].len = std::clamp(len, 2.0f, float(kBufLen - 4));
      // Loop gain for t60 seconds of 60 dB decay at the fundamental
      comb_[i].gain = std::pow(10.0f, -3.0f * comb_[i].len / (sr * t60_));
      // Fractional-delay realization (2026-08-13): the linear-interp read
      // attenuates by |1-frac+frac*e^{-jw0}| per period — negligible low,
      // but at short delays the frac-dependent loss swamps t60 (measured
      // E7: -40 dB/s vs t60's -11) and made octave 7's sustain alternate
      // ring/dead note to note purely on where frac landed. Gain
      // compensation is NOT viable (a gain > 1 destabilizes the comb's
      // low resonances where the interp is lossless — measured, blew up
      // everything above C7). Correct fix: ALLPASS fractional delay
      // (Jaffe-Smith), unity magnitude at every frequency. Applied only
      // where linear loss is material (w0 >= kApReadW0 ~ 764 Hz) so the
      // approved low/mid keyboard stays bit-identical.
      if (w0 >= kApReadW0) {
        int M = int(std::floor(comb_[i].len));
        float frac = comb_[i].len - float(M);
        if (frac < 0.1f) { M -= 1; frac += 1.0f; }  // keep eta away from 1
        float lo = -0.5f, hi = 0.95f;  // pd(eta) monotonically decreasing
        for (int it = 0; it < 24; ++it) {
          float mid = 0.5f * (lo + hi);
          if (apfrac_phase_delay(mid, w0) > frac) lo = mid; else hi = mid;
        }
        comb_[i].apEta = 0.5f * (lo + hi);
        comb_[i].lenInt = std::max(1, M);
        comb_[i].apRead = true;
      } else {
        comb_[i].apRead = false;
      }
    }

    // Normalize the global feedback by comb resonance headroom: peak gain of
    // a feedback comb is ~1/(1 - loopGain), so a raw coefficient would blow
    // up as soon as t60 grows. fbScale keeps open-loop gain ~= fbCoeff.
    float gmax = 0.0f;
    for (int i = 0; i < numCombs_; ++i) gmax = std::max(gmax, comb_[i].gain);
    fbScale_ = fbCoeff_ * (1.0f - gmax);

    dispLen_ = std::clamp(int(std::lround(period)), 2, kBufLen - 4);
    hpR_ = std::exp(-2.0f * 3.14159265f * inharmHp_ / sr);
    dcR_ = 1.0f - 2.0f * 3.14159265f * 20.0f / sr;  // 20 Hz DC blocker

    initialized_ = true;
  }

  std::shared_ptr<ValueSource> source_;
  std::shared_ptr<ValueSource> damper_;
  std::shared_ptr<ValueSource> frequency_;
  std::shared_ptr<ValueSource> amplitude_;
  int sampleRate_;

  // Config
  int   numCombs_{3};
  float detune_{1.5f};
  float t60_{6.0f};
  float brightness_{0.6f};
  float dispersion_{0.12f};
  float inharmGain_{0.25f};
  float inharmFb_{0.90f};
  float inharmHp_{150.0f};
  float ap_[3]{0.55f, 0.35f, 0.20f};
  float fbCoeff_{0.2f};
  float exciteGain_{1.0f};
  float direct_{0.2f};

  // Per-note state
  bool  initialized_{false};
  bool  dispActive_{false};
  float dispEff_{0.12f};   // per-note effective dispersion (shed at the top)
  Comb  comb_[kMaxCombs];
  DispLoop disp_;
  int   dispLen_{100};
  float fbScale_{0.0f};
  int   wpos_{0};
  float hpR_{0.98f};
  float dcR_{0.997f};
  float lpDelay_{0.0f};
  float dcX_{0.0f}, dcY_{0.0f};
  float lastOut_{0.0f};
  float cur_{0.0f};
};

} // namespace mforce
