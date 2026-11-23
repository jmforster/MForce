#pragma once
// Helpers the source tests share, and the suites one file runs from another.
#include "mforce/testing/check.h"
#include "source/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/descriptors.h"
#include "mforce/core/dsp_value_source.h"
#include "mforce/core/pins.h"
#include "mforce/core/randomizer.h"
#include "mforce/source/additive/formant_peaks.h"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;


inline bool same_bits(float a, float b) {
    return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b);
}

using CurveKnots = std::vector<std::pair<float, float>>;
namespace {
struct UnitRamp final : mforce::Generator {
  float v_{0.0f};
  float compute() override { return v_ += 1.0f; }
  Range range() const override { return Range::Unipolar; }
  const char* type_name() const override { return "UnitRamp"; }
};
struct Counter final : Generator {
    int computes{0};
    float compute() override { return float(++computes); }
    const char* type_name() const override { return "Counter"; }
};
// Counts how often a pin's owner prepared and advanced it.
struct PinCounter final : mforce::Generator {
  int starts{0}, nexts{0}, rate{0}, ctxPrepares{0};
  float level{1.0f};
  void on_prepare(const mforce::RenderContext& c) override { rate = c.sampleRate; ++ctxPrepares; }
  void on_start(int) override { ++starts; }
  float compute() override { ++nexts; return level; }
  const char* type_name() const override { return "PinCounter"; }
};
inline std::string param_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.param_descriptors()) { out += d.name; out += ','; }
    return out;
}
inline float param_default(const mforce::Component& c, const char* name) {
    for (const auto& d : c.param_descriptors())
        if (std::string(d.name) == name) return d.default_value;
    return -999.0f;
}
inline std::string input_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.input_descriptors()) { out += d.name; out += ','; }
    return out;
}
inline std::string setting_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.setting_descriptors()) { out += d.name; out += ','; }
    return out;
}
// Logs the order in which its consumers advance it, and counts prepares.
struct OrderProbe final : mforce::Generator {
    OrderProbe(std::vector<int>* l, int i) : log(l), id(i) {}
    std::vector<int>* log;
    int id;
    int starts{0};
    void on_start(int) override { ++starts; }
    float compute() override { log->push_back(id); return 1.0f; }
    const char* type_name() const override { return "OrderProbe"; }
};
inline std::string array_names(const mforce::Component& c) {
    std::string out;
    for (const auto& d : c.array_descriptors()) { out += d.name; out += ','; }
    return out;
}
// The connectable pins of a node in registration order, which is the order
// the base advances them.
inline std::string pin_order(const mforce::Component& c) {
    std::string out;
    for (const auto* p : c.pins())
        if (p->kind == mforce::PinKind::Param || p->kind == mforce::PinKind::Input) {
            out += p->name;
            out += ',';
        }
    return out;
}
inline const SettingDescriptor* find_setting(const Component& c, const char* name) {
    for (const auto& d : c.setting_descriptors())
        if (std::string(d.name) == name) return &d;
    return nullptr;
}
inline std::shared_ptr<ConstantSource> konst(float v) { return std::make_shared<ConstantSource>(v); }
}  // namespace

inline bool same_bits(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() &&
           std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

// The 2,000-point log grid from 20 Hz to 20 kHz the formant twins share.
inline std::vector<float> formant_log_grid() {
    std::vector<float> freqs;
    const float lo = std::log10(20.0f), hi = std::log10(20000.0f);
    for (int i = 0; i < 2000; ++i)
        freqs.push_back(std::pow(10.0f, lo + (hi - lo) * float(i) / 1999.0f));
    return freqs;
}
namespace formant_peaks_twin {
struct Row { float f, g, w, p; };
inline bool old_peak_contains(const Row& r, float freq) {
    const float lo = r.f - r.w * 0.5f;
    const float hi = r.f + r.w * 0.5f;
    return freq > lo && freq < hi;
}
inline float old_peak_gain(const Row& r, float freq) {
    const float lo = r.f - r.w * 0.5f;
    const float hi = r.f + r.w * 0.5f;
    if (freq <= lo || freq >= hi) return 0.0f;
    if (freq < r.f)
        return std::pow((freq - lo) / (r.f - lo), r.p) * r.g;
    return std::pow((hi - freq) / (hi - r.f), r.p) * r.g;
}
inline bool old_spectrum_contains(const std::vector<Row>& rows, float freq) {
    for (const auto& r : rows)
        if (old_peak_contains(r, freq)) return true;
    return false;
}
inline float old_spectrum_gain(const std::vector<Row>& rows, float freq) {
    float g = 0.0f;
    for (const auto& r : rows)
        if (old_peak_contains(r, freq))
            g = std::max(g, old_peak_gain(r, freq));
    return g;
}
static const std::vector<std::vector<Row>> kSets = {
    {{1000.0f, 1.0f, 500.0f, 2.0f}},                                                    // one row
    {{700.0f, 1.0f, 600.0f, 0.5f}, {900.0f, 0.0f, 500.0f, 2.0f}, {1100.0f, 0.8f, 800.0f, 6.0f}},   // overlapping; a gain-0 row; three powers
    {{800.0f, 1.0f, 640.0f, 2.0f}, {1150.0f, 0.631f, 720.0f, 2.0f}, {2800.0f, 0.1f, 960.0f, 2.0f},
     {3500.0f, 0.0158f, 1040.0f, 2.0f}, {4950.0f, 0.001f, 1120.0f, 2.0f}},              // a vowel's five
    {{300.0f, 2.5f, 1000.0f, 1.0f}, {300.0f, 2.5f, 1000.0f, 1.0f}},                     // two equal rows
    {{220.0f, 0.7f, 400.0f, 1.0f}, {850.0f, 1.0f, 900.0f, 2.0f}, {1850.0f, 0.6f, 1200.0f, 1.0f}, {3100.0f, 0.65f, 1000.0f, 1.5f}},
};
inline void fill(FormantPeaks& peaks, const std::vector<Row>& set) {
    std::vector<float> f, g, w, p;
    for (const auto& r : set) { f.push_back(r.f); g.push_back(r.g); w.push_back(r.w); p.push_back(r.p); }
    peaks.set_array("frequency", f);
    peaks.set_array("gain", g);
    peaks.set_array("width", w);
    peaks.set_array("power", p);
}
}  // namespace formant_peaks_twin
namespace node_twins {
// A seeded white noise in [-1, 1].
struct NoiseFeed final : Generator {
    Randomizer rng;
    explicit NoiseFeed(uint32_t seed) : rng(seed) {}
    float compute() override { return rng.valuePN(); }
    const char* type_name() const override { return "NoiseFeed"; }
};
inline std::shared_ptr<ValueSource> c(float v) { return std::make_shared<ConstantSource>(v); }
inline std::shared_ptr<ValueSource> noise(uint32_t seed) { return std::make_shared<NoiseFeed>(seed); }
inline uint64_t base(const std::string& name) {
    for (const auto& [n, h] : kBase) if (name == n) return h;
    std::printf("  no recorded case '%s'\n", name.c_str());
    return 0;
}
}  // namespace node_twins

void run_combine_mod_pin_shape_tests();
void run_combine_mod_default_tests();
void run_combine_mod_auto_pin_tests();
void run_combine_mod_manual_pin_tests();
void run_combine_mod_shaper_timing_tests();
