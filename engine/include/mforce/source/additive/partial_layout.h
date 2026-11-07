#pragma once
// PartialLayout: the partial arrays, their originals and the expansion.
#include "mforce/source/additive/expand_rule.h"
#include "mforce/core/randomizer.h"
#include <cmath>
#include <vector>

namespace mforce {

// The six per-partial arrays a set lays out — the start and end multipliers,
// amplitudes and phase offsets — with the originals the last rebuild kept
// and the expansion that derives sub-partials from them per note. The arrays
// are the layout's face: a set's init_arrays writes them and the render body
// reads them; the originals are private, so a note's expansion can only start
// from what snapshot kept. A value type the owner holds.
struct PartialLayout {
  std::vector<float> mult1, mult2, ampl1, ampl2, po1, po2;

  int count() const { return int(mult1.size()); }
  int original_count() const { return int(origMult1_.size()); }

  // The originals the expand rule starts every note from: the arrays as the
  // last rebuild made them, before any expansion.
  void snapshot() {
    origMult1_ = mult1; origMult2_ = mult2;
    origAmpl1_ = ampl1; origAmpl2_ = ampl2;
    origPo1_ = po1;     origPo2_ = po2;
  }

  // Back to the originals; every assign fits the capacity reserve_expanded gave.
  void restore() {
    mult1.assign(origMult1_.begin(), origMult1_.end()); mult2.assign(origMult2_.begin(), origMult2_.end());
    ampl1.assign(origAmpl1_.begin(), origAmpl1_.end()); ampl2.assign(origAmpl2_.begin(), origAmpl2_.end());
    po1.assign(origPo1_.begin(), origPo1_.end());       po2.assign(origPo2_.begin(), origPo2_.end());
  }

  // Capacity for the rule's full expansion of the originals — every
  // recursion multiplies the count by 2 * count + 1 — on the arrays and the
  // scratch, so expand allocates nothing. Returns the expanded count.
  int reserve_expanded(const ExpandRule& rule) {
    int n = original_count();
    for (int r = 0; r <= rule.recurse; ++r) n *= rule.count * 2 + 1;
    for (auto* v : {&mult1, &mult2, &ampl1, &ampl2, &po1, &po2,
                    &exM1_, &exM2_, &exA1_, &exA2_, &exP1_, &exP2_})
      v->reserve(size_t(n));
    return n;
  }

  // One expansion of the arrays by the rule: count sub-partials each side of
  // every partial, spaced in semitones, detuned from the rule's stream,
  // their amplitudes a power curve down to loPct, their phases stepped by po.
  void expand(const ExpandRule& er, Randomizer& expandRng) {
    int origN = int(mult1.size());
    int newN = origN * (er.count * 2 + 1);
    auto& m1 = exM1_; auto& m2 = exM2_; auto& a1 = exA1_;
    auto& a2 = exA2_; auto& p1 = exP1_; auto& p2 = exP2_;
    for (auto* v : {&m1, &m2, &a1, &a2, &p1, &p2}) v->assign(size_t(newN), 0.0f);   // within capacity

    for (int i = 0; i < origN; ++i) {
      int base = i * (er.count * 2 + 1);

      // Left sub-partials
      for (int j = 0; j < er.count; ++j) {
        int idx = base + j;
        float semis1 = float(er.count - j) * er.spacing1;
        float semis2 = float(er.count - j) * er.spacing2;

        m1[idx] = mult1[i] / std::pow(2.0f, semis1 / 12.0f) * (1.0f + expandRng.valuePN() * er.dt1);
        m2[idx] = mult2[i] / std::pow(2.0f, semis2 / 12.0f) * (1.0f + expandRng.valuePN() * er.dt2);

        float t = float(j) / float(er.count);
        a1[idx] = ampl1[i] * er.loPct1 + ampl1[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
        a2[idx] = ampl2[i] * er.loPct2 + ampl2[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);

        p1[idx] = std::fmod(po1[i] + float(j + 1) / float(er.count) * er.po1, 1.0f);
        p2[idx] = std::fmod(po2[i] + float(j + 1) / float(er.count) * er.po2, 1.0f);
      }

      // Primary (center)
      int cIdx = base + er.count;
      m1[cIdx] = mult1[i]; m2[cIdx] = mult2[i];
      a1[cIdx] = ampl1[i]; a2[cIdx] = ampl2[i];
      p1[cIdx] = po1[i];   p2[cIdx] = po2[i];

      // Right sub-partials
      for (int j = 0; j < er.count; ++j) {
        int idx = base + er.count + 1 + j;
        float semis1 = float(j + 1) * er.spacing1;
        float semis2 = float(j + 1) * er.spacing2;

        m1[idx] = mult1[i] * std::pow(2.0f, semis1 / 12.0f);
        m2[idx] = mult2[i] * std::pow(2.0f, semis2 / 12.0f);

        float t = float(er.count - j - 1) / float(er.count);
        a1[idx] = ampl1[i] * er.loPct1 + ampl1[i] * (1.0f - er.loPct1) * std::pow(t, er.power1);
        a2[idx] = ampl2[i] * er.loPct2 + ampl2[i] * (1.0f - er.loPct2) * std::pow(t, er.power2);

        p1[idx] = std::fmod(po1[i] + float(er.count - j) / float(er.count) * er.po1, 1.0f);
        p2[idx] = std::fmod(po2[i] + float(er.count - j) / float(er.count) * er.po2, 1.0f);
      }
    }

    mult1.assign(m1.begin(), m1.end()); mult2.assign(m2.begin(), m2.end());
    ampl1.assign(a1.begin(), a1.end()); ampl2.assign(a2.begin(), a2.end());
    po1.assign(p1.begin(), p1.end());   po2.assign(p2.begin(), p2.end());
  }

private:
  std::vector<float> origMult1_, origMult2_, origAmpl1_, origAmpl2_, origPo1_, origPo2_;
  std::vector<float> exM1_, exM2_, exA1_, exA2_, exP1_, exP2_;   // the expansion's scratch, reserved by reserve_expanded
};

} // namespace mforce
