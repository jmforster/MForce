#include "mforce/testing/check.h"
#include "test_support.h"
#include "pin_order_probe.h"
#include "core/tests/fixtures.h"
#include "mforce/core/component.h"
#include "mforce/core/pins.h"
#include "mforce/core/randomizer.h"
#include "mforce/source/additive/bandwidth_noise.h"
#include "mforce/source/additive/coherent_walk.h"
#include "mforce/source/additive/composite_partials.h"
#include "mforce/source/additive/decay_law.h"
#include "mforce/source/additive/expand_rule.h"
#include "mforce/source/additive/full_partials.h"
#include "mforce/source/additive/onset_gate.h"
#include "mforce/source/additive/pair_trade.h"
#include "mforce/source/additive/partial_layout.h"
#include "mforce/source/additive/partial_set.h"
#include "mforce/source/additive/partials.h"
#include "mforce/source/additive/sequence_partials.h"
#include "mforce/testing/alloc_probe.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <utility>
#include <vector>

using namespace mforce;

namespace partials_twin {
inline void init(W& w, int baseLen, float evolve, Randomizer& rng) {
    w.cur = rng.valuePN();
    w.target = rng.valuePN();
    w.len = seg_len(baseLen, evolve, rng);
    w.pos = int(rng.range(0.0f, float(w.len)));
}
}  // namespace partials_twin

void run_coherent_walk_twin_tests() {
    std::printf("coherent walk: the motion block against the layer\n");
    int bad = 0, rows = 0;
    for (auto rh : {std::pair<float, float>{48000.0f, 4.0f}, std::pair<float, float>{44100.0f, 7.3f}})
    for (int n : {1, 8, 61})
        for (float coh : {0.0f, 0.4f, 0.5f, 0.59f, 1.0f})
            for (float evolve : {0.0f, 0.3f})
                for (uint32_t noteBase : {0x1234u, 0xBEEFu}) {
                    const float rate = rh.first, hz = rh.second;
                    // the deleted block, as Partials::start and next ran it
                    int moLen = std::max(1, int(rate / std::max(0.01f, hz)));
                    Randomizer moSharedRng(stream_seed(noteBase, 0, kRngMotionShared));
                    partials_twin::W moShared;
                    partials_twin::init(moShared, moLen, evolve, moSharedRng);
                    std::vector<partials_twin::W> moWalks(static_cast<size_t>(n));
                    std::vector<Randomizer> moRng(size_t(n), Randomizer(0u));
                    for (int i = 0; i < n; ++i) {
                        moRng[size_t(i)] = Randomizer(stream_seed(noteBase, uint32_t(i), kRngMotionInd));
                        partials_twin::init(moWalks[size_t(i)], moLen, evolve, moRng[size_t(i)]);
                    }
                    std::vector<float> moVals(size_t(n), 0.0f);
                    CoherentWalk layer;
                    layer.prepare(n);
                    layer.start(n, noteBase, kRngMotionShared, kRngMotionInd, hz, evolve, rate);
                    for (int k = 0; k < 300; ++k) {
                        float shared = partials_twin::advance(moShared, moLen, evolve, moSharedRng);
                        for (size_t i = 0; i < moWalks.size(); ++i) {
                            float ind = partials_twin::advance(moWalks[i], moLen, evolve, moRng[i]);
                            moVals[i] = coh * shared + (1.0f - coh) * ind;
                        }
                        layer.advance(evolve, coh);
                        for (int i = 0; i < n; ++i) {
                            ++rows;
                            if (!same_bits(moVals[size_t(i)], layer.value(i))) ++bad;
                        }
                    }
                }
    CHECK(bad == 0);
    CHECK(rows == 2 * (1 + 8 + 61) * 5 * 2 * 2 * 300);
}

void run_pair_trade_twin_tests() {
    std::printf("pair trade: the trade block against the layer\n");
    int bad = 0, rows = 0;
    const float rate = 48000.0f;
    for (float hz : {2.0f, 0.37f, 7.3f})
    for (int n : {1, 2, 7, 24})
        for (uint32_t noteBase : {0x1234u, 0xBEEFu}) {
            int trLen = std::max(1, int(rate / std::max(0.01f, hz)));
            Randomizer trRng(stream_seed(noteBase, 0, kRngTrade));
            std::vector<partials_twin::W> trWalks(size_t((n + 1) / 2));
            for (auto& w : trWalks) partials_twin::init(w, trLen, 0.0f, trRng);
            std::vector<float> trVals(size_t((n + 1) / 2), 0.0f);
            PairTrade layer;
            layer.prepare(n);
            layer.start(n, noteBase, kRngTrade, hz, rate);
            for (int k = 0; k < 300; ++k) {
                for (size_t i = 0; i < trWalks.size(); ++i)
                    trVals[i] = partials_twin::advance(trWalks[i], trLen, 0.0f, trRng);
                layer.advance();
                for (size_t i = 0; i < trVals.size(); ++i) {
                    ++rows;
                    if (!same_bits(trVals[i], layer.value(int(i)))) ++bad;
                }
            }
        }
    CHECK(bad == 0);
    CHECK(rows == 3 * (1 + 1 + 4 + 12) * 2 * 300);
}

// The bandwidth noise, the onset gate and the decay law against the blocks
// they replaced: each block retyped here as Partials ran it, both sides on
// identically seeded streams, every output compared bit for bit (a layer's
// state is private and is read through its outputs) — the onset rows with
// t <= 0 included, where an assignment and a
// multiply by zero differ in the zero's sign.
void run_bandwidth_noise_twin_tests() {
    std::printf("bandwidth noise: the per-partial walk against the layer\n");
    int bad = 0, rows = 0;
    const float rate = 48000.0f;
    for (int n : {1, 5, 40})
        for (float hz : {0.5f, 30.0f, 2000.0f})
            for (uint32_t noteBase : {0x1234u, 0xBEEFu}) {
                // the start block
                int bwLen = std::max(1, int(rate / (hz < 1.0f ? 1.0f : hz)));
                std::vector<float> bwCur(size_t(n), 0.0f), bwTarget(size_t(n), 0.0f);
                std::vector<int> bwPos(size_t(n), 0);
                std::vector<Randomizer> bwRng(size_t(n), Randomizer(0u));
                for (int i = 0; i < n; ++i) {
                    bwRng[size_t(i)]    = Randomizer(stream_seed(noteBase, uint32_t(i), kRngBandwidth));
                    bwCur[size_t(i)]    = bwRng[size_t(i)].valuePN();
                    bwTarget[size_t(i)] = bwRng[size_t(i)].valuePN();
                    bwPos[size_t(i)]    = int(bwRng[size_t(i)].range(0.0f, float(bwLen)));
                }
                BandwidthNoise layer;
                layer.prepare(n);
                layer.start(n, noteBase, kRngBandwidth, hz, rate);
                const float dry = std::sqrt(1.0f - 0.37f), wet = std::sqrt(0.37f);
                for (int k = 0; k < 400; ++k)
                    for (int index = 0; index < n; ++index) {
                        // the body's block
                        if (bwPos[size_t(index)] >= bwLen) {
                            bwCur[size_t(index)]    = bwTarget[size_t(index)];
                            bwTarget[size_t(index)] = bwRng[size_t(index)].valuePN();
                            bwPos[size_t(index)]    = 0;
                        }
                        float u = float(bwPos[size_t(index)]) / float(bwLen);
                        float s = u * u * (3.0f - 2.0f * u);
                        float noise = bwCur[size_t(index)] + (bwTarget[size_t(index)] - bwCur[size_t(index)]) * s;
                        ++bwPos[size_t(index)];
                        float expected = dry + wet * noise;
                        float got = layer.factor(index, dry, wet);
                        ++rows;
                        if (!same_bits(expected, got)) ++bad;
                    }
            }
    CHECK(bad == 0);
    CHECK(rows == (1 + 5 + 40) * 3 * 2 * 400);
}

void run_onset_gate_twin_tests() {
    std::printf("onset gate: the delays and the fade against the layer\n");
    int bad = 0, rows = 0, zeros = 0;
    const float rate = 48000.0f;
    for (int n : {1, 6, 33})
        for (float tilt : {-1.0f, -0.5f, 0.0f, 0.5f, 1.0f})
            for (float spread : {0.0f, 0.01f, 0.2f})
                for (uint32_t noteBase : {0x1234u, 0xBEEFu}) {
                    const float fade = 0.0005f;
                    // the start block
                    Randomizer onsetRng(stream_seed(noteBase, 0, kRngOnset));
                    std::vector<float> onsetDelay(static_cast<size_t>(n));
                    float at = std::fabs(tilt);
                    for (int i = 0; i < n; ++i) {
                        float u = onsetRng.value();
                        float r = (n > 1) ? float(i) / float(n - 1) : 0.0f;
                        float ordered = (tilt >= 0.0f) ? r : 1.0f - r;
                        onsetDelay[size_t(i)] = ((1.0f - at) * u + at * ordered) * spread * rate;
                    }
                    float onsetFadeSamples = std::max(1.0f, fade * rate);
                    OnsetGate layer;
                    layer.prepare(n);
                    layer.reset();
                    layer.start(n, noteBase, kRngOnset, spread, tilt, fade, rate);
                    int sampleIdx = 0;
                    for (int k = 0; k < 120; ++k) {
                        ++sampleIdx;
                        layer.advance();
                        for (int index = 0; index < n; ++index) {
                            const float in = (index & 1) ? -0.75f : 0.75f;   // both signs: the zero's sign is the point
                            // the body's block
                            float pampl = in;
                            float t = (float(sampleIdx) - onsetDelay[size_t(index)]) / onsetFadeSamples;
                            if (t <= 0.0f) pampl = 0.0f;
                            else if (t < 1.0f) pampl *= t * t * (3.0f - 2.0f * t);
                            float got = layer.apply(index, in);
                            ++rows;
                            if (t <= 0.0f) ++zeros;
                            if (!same_bits(pampl, got)) ++bad;
                        }
                    }
                }
    CHECK(bad == 0);
    CHECK(zeros > 0);                       // the assignment rows were exercised
    CHECK(rows == (1 + 6 + 33) * 5 * 3 * 2 * 120);
}

void run_decay_law_twin_tests() {
    std::printf("decay law: the factor and the running gain against the layer\n");
    int bad = 0, rows = 0;
    const float pmults[] = {1.0f, 2.5f, 17.0f, 61.3f};
    for (float rate : {48000.0f, 44100.0f})
    for (float decayRate : {0.0f, 1.0f, 40.0f, 400.0f})
        for (float decayExp : {-2.0f, 0.0f, 0.6f, 4.0f}) {
            const int n = 4;
            std::vector<float> decayGain(n, 1.0f), decayState(n, 1.0f);
            for (int i = 0; i < n; ++i) {
                const double r = double(decayRate) * std::pow(double(pmults[i]), double(decayExp));
                decayGain[size_t(i)] = float(std::pow(10.0, -r / (20.0 * double(rate))));
            }
            DecayLaw layer;
            layer.prepare(n);
            layer.start(n);
            for (int i = 0; i < n; ++i) layer.set_gain(i, pmults[i], decayRate, decayExp, rate);
            for (int k = 0; k < 2000; ++k) {
                for (size_t i = 0; i < size_t(n); ++i) decayState[i] *= decayGain[i];
                layer.advance();
                for (int i = 0; i < n; ++i) {
                    ++rows;
                    if (!same_bits(decayState[size_t(i)], layer.gain(i))) ++bad;
                }
            }
        }
    CHECK(bad == 0);
    CHECK(rows == 2 * 4 * 4 * 2000 * 4);
}

// The expansion against the deleted apply_expand_rule: the block retyped here
// over six local arrays as Partials ran it, the layout on the same originals
// and an identically seeded stream, all six arrays compared bit for bit; a
// restore after an expansion gives the snapshot back; a reserved layout
// expands without allocating.
void run_partial_layout_twin_tests() {
    std::printf("partial layout: the expansion against the deleted block\n");
    int bad = 0, rows = 0;
    for (int n : {1, 4, 16})
        for (int count : {1, 2, 3})
            for (int recurse : {0, 1, 2})
                for (uint32_t seed : {0x51u, 0xA7u}) {
                    ExpandRule er;
                    er.count = count; er.recurse = recurse;
                    er.spacing1 = 0.3f; er.spacing2 = 0.4f; er.dt1 = 0.02f; er.dt2 = 0.03f;
                    er.loPct1 = 0.2f; er.loPct2 = 0.25f; er.power1 = 1.5f; er.power2 = 0.7f;
                    er.po1 = 0.25f; er.po2 = 0.5f;
                    // the originals: a deterministic layout
                    std::vector<float> mult1_(static_cast<size_t>(n)), mult2_(static_cast<size_t>(n)), ampl1_(static_cast<size_t>(n)), ampl2_(static_cast<size_t>(n)), po1_(static_cast<size_t>(n)), po2_(static_cast<size_t>(n));
                    for (int i = 0; i < n; ++i) {
                        mult1_[size_t(i)] = float(i + 1) * 1.01f; mult2_[size_t(i)] = float(i + 1);
                        ampl1_[size_t(i)] = 1.0f / float(i + 1);  ampl2_[size_t(i)] = 0.5f / float(i + 1);
                        po1_[size_t(i)] = 0.1f * float(i) - 0.25f; po2_[size_t(i)] = 0.07f * float(i) - 0.1f;   // negative for the low partials
                    }
                    PartialLayout layout;
                    layout.mult1 = mult1_; layout.mult2 = mult2_; layout.ampl1 = ampl1_; layout.ampl2 = ampl2_;
                    layout.po1 = po1_; layout.po2 = po2_;
                    layout.snapshot();
                    const std::vector<float> oM1 = mult1_, oM2 = mult2_, oA1 = ampl1_, oA2 = ampl2_, oP1 = po1_, oP2 = po2_;
                    const int expanded = layout.reserve_expanded(er);
                    layout.restore();   // the note's path: the originals back, then the expansion
                    std::vector<float> exM1_, exM2_, exA1_, exA2_, exP1_, exP2_;
                    Randomizer expandRng_(seed), layoutRng(seed);
                    for (int r = 0; r <= er.recurse; ++r) {
                        // the deleted block, verbatim over the local arrays
                        int origN = int(mult1_.size());
                        int newN = origN * (er.count * 2 + 1);
                        auto& m1 = exM1_; auto& m2 = exM2_; auto& a1 = exA1_;
                        auto& a2 = exA2_; auto& p1 = exP1_; auto& p2 = exP2_;
                        for (auto* v : {&m1, &m2, &a1, &a2, &p1, &p2}) v->assign(size_t(newN), 0.0f);
                        for (int i = 0; i < origN; ++i) {
                            int base = i * (er.count * 2 + 1);
                            for (int j = 0; j < er.count; ++j) {
                                int idx = base + j;
                                float semis1 = float(er.count - j) * er.spacing1;
                                float semis2 = float(er.count - j) * er.spacing2;
                                m1[idx] = mult1_[i] / std::pow(2.0f, semis1 / 12.0f) * (1.0f + expandRng_.valuePN() * er.dt1);
                                m2[idx] = mult2_[i] / std::pow(2.0f, semis2 / 12.0f) * (1.0f + expandRng_.valuePN() * er.dt2);
                                float t = float(j) / float(er.count);
                                a1[idx] = ampl1_[i] * er.loPct1 + ampl1_[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
                                a2[idx] = ampl2_[i] * er.loPct2 + ampl2_[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);
                                p1[idx] = std::fmod(po1_[i] + float(j + 1) / float(er.count) * er.po1, 1.0f);
                                p2[idx] = std::fmod(po2_[i] + float(j + 1) / float(er.count) * er.po2, 1.0f);
                            }
                            int cIdx = base + er.count;
                            m1[cIdx] = mult1_[i]; m2[cIdx] = mult2_[i];
                            a1[cIdx] = ampl1_[i]; a2[cIdx] = ampl2_[i];
                            p1[cIdx] = po1_[i];   p2[cIdx] = po2_[i];
                            for (int j = 0; j < er.count; ++j) {
                                int idx = base + er.count + 1 + j;
                                float semis1 = float(j + 1) * er.spacing1;
                                float semis2 = float(j + 1) * er.spacing2;
                                m1[idx] = mult1_[i] * std::pow(2.0f, semis1 / 12.0f);
                                m2[idx] = mult2_[i] * std::pow(2.0f, semis2 / 12.0f);
                                float t = float(er.count - j - 1) / float(er.count);
                                a1[idx] = ampl1_[i] * er.loPct1 + ampl1_[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
                                a2[idx] = ampl2_[i] * er.loPct2 + ampl2_[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);
                                p1[idx] = std::fmod(po1_[i] + float(er.count - j) / float(er.count) * er.po1, 1.0f);
                                p2[idx] = std::fmod(po2_[i] + float(er.count - j) / float(er.count) * er.po2, 1.0f);
                            }
                        }
                        mult1_.assign(m1.begin(), m1.end()); mult2_.assign(m2.begin(), m2.end());
                        ampl1_.assign(a1.begin(), a1.end()); ampl2_.assign(a2.begin(), a2.end());
                        po1_.assign(p1.begin(), p1.end());   po2_.assign(p2.begin(), p2.end());
                        layout.expand(er, layoutRng);
                    }
                    CHECK(layout.count() == expanded && int(mult1_.size()) == expanded);
                    const std::vector<float>* mine[] = {&mult1_, &mult2_, &ampl1_, &ampl2_, &po1_, &po2_};
                    const std::vector<float>* theirs[] = {&layout.mult1, &layout.mult2, &layout.ampl1, &layout.ampl2, &layout.po1, &layout.po2};
                    for (int a = 0; a < 6; ++a)
                        for (size_t i = 0; i < mine[a]->size(); ++i) {
                            ++rows;
                            if (!same_bits((*mine[a])[i], (*theirs[a])[i])) ++bad;
                        }
                    // restore gives the snapshot back, all six arrays
                    layout.restore();
                    CHECK(layout.count() == n);
                    auto bitsEqual = [](const std::vector<float>& a, const std::vector<float>& b) {
                        if (a.size() != b.size()) return false;
                        for (size_t i = 0; i < a.size(); ++i) if (!same_bits(a[i], b[i])) return false;
                        return true;
                    };
                    CHECK(bitsEqual(layout.mult1, oM1) && bitsEqual(layout.mult2, oM2) && bitsEqual(layout.ampl1, oA1)
                          && bitsEqual(layout.ampl2, oA2) && bitsEqual(layout.po1, oP1) && bitsEqual(layout.po2, oP2));
                }
    CHECK(bad == 0);
    CHECK(rows > 0);
    // a reserved layout expands without allocating
    {
        ExpandRule er;
        er.count = 2; er.recurse = 1;
        PartialLayout layout;
        for (int i = 0; i < 8; ++i) {
            layout.mult1.push_back(float(i + 1)); layout.mult2.push_back(float(i + 1));
            layout.ampl1.push_back(1.0f); layout.ampl2.push_back(1.0f);
            layout.po1.push_back(0.0f); layout.po2.push_back(0.0f);
        }
        layout.snapshot();
        layout.reserve_expanded(er);
        Randomizer rng(7u);
        alloc_probe::reset();
        alloc_probe::arm();
        layout.restore();
        for (int r = 0; r <= er.recurse; ++r) layout.expand(er, rng);
        alloc_probe::disarm();
        CHECK(alloc_probe::count() == 0);
        CHECK(layout.count() == 8 * 5 * 5);
    }
}
void run_partial_set_lifecycle_tests() {
    std::printf("partial set: the base drives the pins\n");
    RenderContext ctx{48000};
    const char* names[] = {"multEnv", "amplEnv", "poEnv", "roEnv", "dtEnv", "bwEnv", "motionEnv", "shimmerEnv"};
    {
        FullPartials full;
        std::vector<std::shared_ptr<PinCounter>> probes;
        for (const char* n : names) {
            probes.push_back(std::make_shared<PinCounter>());
            CHECK(full.set_param(n, probes.back()));
        }
        PartialSet& set = full;
        set.prepare(ctx); set.start(4800);
        for (int i = 0; i < 7; ++i) set.next();
        for (const auto& p : probes) CHECK(p->ctxPrepares == 1 && p->starts == 1 && p->nexts == 7);
    }
    {
        FullPartials full;
        std::vector<std::string> log;
        for (const char* n : names) {
            auto o = std::make_shared<OrderProbe>();
            o->log = &log; o->tag = n;
            CHECK(full.set_param(n, o));
        }
        full.prepare(ctx); full.start(4800); full.next();
        std::string seen;
        for (const auto& s : log) seen += s + ",";
        CHECK(seen == "multEnv,amplEnv,poEnv,roEnv,dtEnv,bwEnv,motionEnv,shimmerEnv,");
    }
    {
        CompositePartials comp;
        auto a = std::make_shared<FullPartials>();
        auto b = std::make_shared<SequencePartials>();
        auto pa = std::make_shared<PinCounter>();
        auto pb = std::make_shared<PinCounter>();
        CHECK(a->set_param("multEnv", pa) && b->set_param("amplEnv", pb));
        CHECK(comp.add_param("partials", a) && comp.add_param("partials", b));
        PartialSet& set = comp;
        set.prepare(ctx); set.start(4800);
        for (int i = 0; i < 7; ++i) set.next();
        CHECK(pa->ctxPrepares == 1 && pa->starts == 1 && pa->nexts == 7);
        CHECK(pb->ctxPrepares == 1 && pb->starts == 1 && pb->nexts == 7);
        CHECK(comp.partial_count() == 60);
    }
}
