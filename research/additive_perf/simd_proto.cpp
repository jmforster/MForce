// ---------------------------------------------------------------------------
// Additive partial loop — isolated SIMD prototype (item 8 stage 2d, phase 1)
//
// Spec: docs/superpowers/specs/2026-07-31-additive-simd-soa-design.md
//
// This is deliberately NOT wired into the engine. Two theories about where the
// 57 ns/sample/partial goes have already died on controlled A/B (branchless
// full-period sin: a wash; 4x unroll with split accumulators: slower). The
// spec's phase 1 is therefore: reproduce the hot body over synthetic arrays,
// compare a faithful scalar control against explicit AVX2, and ABORT the whole
// stage if the vector version does not beat scalar by at least 2x here — where
// no engine structure is in the way.
//
// The body replicated is Partials::partial_value_impl with all optional layers
// OFF (motion/shimmer/trade/onset/bandwidth), which is the common case and the
// one the ablation harness measured. Streams: pmultCache, po1, po2, dtVals,
// partialPos, partialLPO, rolloffCache, ampl1, ampl2 (9 reads) + partialPos,
// partialLPO (2 writes).
//
// Build (no CMake, no engine target touched):
//   cl /O2 /Ob2 /DNDEBUG /EHsc /std:c++17 simd_proto.cpp
// Run: simd_proto.exe [samples]
// ---------------------------------------------------------------------------
#include <immintrin.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <chrono>
#include <vector>
#include <limits>
#include <algorithm>

static constexpr float CUTOFF = 16000.0f;

// --- engine's fast_sin_turns, verbatim (core/fast_math.h) ------------------
static constexpr float K1 =  6.2831851603e+00f;
static constexpr float K3 = -4.1341655081e+01f;
static constexpr float K5 =  8.1601007325e+01f;
static constexpr float K7 = -7.6549859620e+01f;
static constexpr float K9 =  3.9537311267e+01f;

inline float fast_sin_turns(float x) {
  float s = 1.0f;
  if (x >= 0.5f) { x -= 0.5f; s = -1.0f; }
  if (x > 0.25f) x = 0.5f - x;
  const float u = x * x;
  float p = K9;
  p = p * u + K7;
  p = p * u + K5;
  p = p * u + K3;
  p = p * u + K1;
  return s * (x * p);
}

// --- same polynomial, 8 lanes ---------------------------------------------
inline __m256 fast_sin_turns8(__m256 x) {
  const __m256 half    = _mm256_set1_ps(0.5f);
  const __m256 quarter = _mm256_set1_ps(0.25f);
  const __m256 one     = _mm256_set1_ps(1.0f);
  const __m256 mone    = _mm256_set1_ps(-1.0f);

  __m256 m1 = _mm256_cmp_ps(x, half, _CMP_GE_OQ);
  x = _mm256_blendv_ps(x, _mm256_sub_ps(x, half), m1);
  __m256 s = _mm256_blendv_ps(one, mone, m1);

  __m256 m2 = _mm256_cmp_ps(x, quarter, _CMP_GT_OQ);
  x = _mm256_blendv_ps(x, _mm256_sub_ps(half, x), m2);

  __m256 u = _mm256_mul_ps(x, x);
  __m256 p = _mm256_set1_ps(K9);
  p = _mm256_fmadd_ps(p, u, _mm256_set1_ps(K7));
  p = _mm256_fmadd_ps(p, u, _mm256_set1_ps(K5));
  p = _mm256_fmadd_ps(p, u, _mm256_set1_ps(K3));
  p = _mm256_fmadd_ps(p, u, _mm256_set1_ps(K1));
  return _mm256_mul_ps(s, _mm256_mul_ps(x, p));
}

// --- synthetic partial set -------------------------------------------------
struct Arrays {
  int n = 0;
  std::vector<float> pmult, po1, po2, dt, rolloff, ampl1, ampl2;
  std::vector<float> pos, lpo;      // mutable state
  std::vector<float> pos0, lpo0;    // pristine copies for re-seeding

  void build(int count, float base) {
    n = count;
    auto rz = [](std::vector<float>& v, int m) { v.assign(m, 0.0f); };
    rz(pmult, n); rz(po1, n); rz(po2, n); rz(dt, n);
    rz(rolloff, n); rz(ampl1, n); rz(ampl2, n); rz(pos, n); rz(lpo, n);
    unsigned s = 12345u;
    auto rnd = [&s]() { s = s * 1664525u + 1013904223u;
                        return float((s >> 8) & 0xFFFF) / 65535.0f; };
    for (int i = 0; i < n; ++i) {
      float m = float(i + 1);
      pmult[i]   = m;
      po1[i]     = rnd();
      po2[i]     = rnd();
      dt[i]      = rnd() - 0.5f;
      rolloff[i] = 1.0f / std::pow(m, 1.2f);
      ampl1[i]   = rolloff[i];
      ampl2[i]   = rolloff[i] * 0.8f;
      pos[i]     = rnd();
      lpo[i]     = po1[i];
    }
    // base tuned so a realistic fraction of partials sits past the 16 kHz
    // cutoff (that branch is what blocks auto-vectorization).
    (void)base;
    pos0 = pos; lpo0 = lpo;
  }
  void reseed() { pos = pos0; lpo = lpo0; }
};

// --- scalar control: faithful port of partial_value_impl -------------------
float run_scalar(Arrays& A, int samples, float frequency, float rate,
                 float amplitude, float sPoE, float sDt, float sAmplE,
                 float phaseDiff)
{
  float sink = 0.0f;
  const int n = A.n;
  for (int t = 0; t < samples; ++t) {
    float acc = 0.0f;
    for (int i = 0; i < n; ++i) {
      float pmult = A.pmult[i];
      float ppo   = A.po1[i] + (A.po2[i] - A.po1[i]) * sPoE;
      float d     = sDt * A.dt[i];
      float pfreq = pmult * frequency * (1.0f + d);
      if (pfreq > CUTOFF) continue;       // NaN protocol, state NOT advanced
      float x = A.pos[i] + pfreq / rate + phaseDiff + (ppo - A.lpo[i]);
      x = x - std::truncf(x);
      if (x < 0.0f) x += 1.0f;
      A.pos[i] = x;
      A.lpo[i] = ppo;
      float fade = (pfreq < CUTOFF - 1000.0f) ? 1.0f : (CUTOFF - pfreq) / 1000.0f;
      float pampl = amplitude *
          (A.ampl1[i] + (A.ampl2[i] - A.ampl1[i]) * sAmplE) *
          A.rolloff[i] * fade;
      acc += fast_sin_turns(A.pos[i]) * pampl;
    }
    sink += acc;
  }
  return sink;
}

// --- scalar variant B: truncf replaced by an int round-trip ----------------
// Hypothesis under test: under the engine's Release flags (/O2, SSE2 baseline,
// no /arch), MSVC emits a CRT CALL for std::truncf rather than the SSE4.1
// roundss instruction, costing a function call per partial per sample. If so
// the scalar loop is libm-bound and this variant alone moves the number —
// with no vectorization and no layout change.
float run_scalar_cast(Arrays& A, int samples, float frequency, float rate,
                      float amplitude, float sPoE, float sDt, float sAmplE,
                      float phaseDiff)
{
  float sink = 0.0f;
  const int n = A.n;
  for (int t = 0; t < samples; ++t) {
    float acc = 0.0f;
    for (int i = 0; i < n; ++i) {
      float pmult = A.pmult[i];
      float ppo   = A.po1[i] + (A.po2[i] - A.po1[i]) * sPoE;
      float d     = sDt * A.dt[i];
      float pfreq = pmult * frequency * (1.0f + d);
      if (pfreq > CUTOFF) continue;
      float x = A.pos[i] + pfreq / rate + phaseDiff + (ppo - A.lpo[i]);
      x = x - float(int(x));            // |x| < 2 here — exact, same result
      if (x < 0.0f) x += 1.0f;
      A.pos[i] = x;
      A.lpo[i] = ppo;
      float fade = (pfreq < CUTOFF - 1000.0f) ? 1.0f : (CUTOFF - pfreq) / 1000.0f;
      float pampl = amplitude *
          (A.ampl1[i] + (A.ampl2[i] - A.ampl1[i]) * sAmplE) *
          A.rolloff[i] * fade;
      acc += fast_sin_turns(A.pos[i]) * pampl;
    }
    sink += acc;
  }
  return sink;
}

// --- scalar variant C: cast-trunc AND reciprocal-multiply ------------------
// The SIMD version hoists 1/rate out of the loop and multiplies. The scalar
// body does a hardware float DIVIDE per partial per sample (~11-14 cycle
// latency, poor throughput). That is not a vectorization win — it is available
// to the scalar loop today. This rung separates "SIMD" from "two scalar fixes
// the engine could take with no layout change".
// 1/rate is exact only for power-of-two rates; at 48000 the reciprocal
// multiply differs from the divide in the last bit, so this rung is NOT
// bit-exact and the divergence print below is expected to fire for it.
float run_scalar_cast_recip(Arrays& A, int samples, float frequency, float rate,
                            float amplitude, float sPoE, float sDt, float sAmplE,
                            float phaseDiff)
{
  float sink = 0.0f;
  const int n = A.n;
  const float invRate = 1.0f / rate;
  for (int t = 0; t < samples; ++t) {
    float acc = 0.0f;
    for (int i = 0; i < n; ++i) {
      float pmult = A.pmult[i];
      float ppo   = A.po1[i] + (A.po2[i] - A.po1[i]) * sPoE;
      float d     = sDt * A.dt[i];
      float pfreq = pmult * frequency * (1.0f + d);
      if (pfreq > CUTOFF) continue;
      float x = A.pos[i] + pfreq * invRate + phaseDiff + (ppo - A.lpo[i]);
      x = x - float(int(x));
      if (x < 0.0f) x += 1.0f;
      A.pos[i] = x;
      A.lpo[i] = ppo;
      float fade = (pfreq < CUTOFF - 1000.0f) ? 1.0f : (CUTOFF - pfreq) * 0.001f;
      float pampl = amplitude *
          (A.ampl1[i] + (A.ampl2[i] - A.ampl1[i]) * sAmplE) *
          A.rolloff[i] * fade;
      acc += fast_sin_turns(A.pos[i]) * pampl;
    }
    sink += acc;
  }
  return sink;
}

// --- AVX2: 8 partials per iteration ---------------------------------------
float run_simd(Arrays& A, int samples, float frequency, float rate,
               float amplitude, float sPoE, float sDt, float sAmplE,
               float phaseDiff)
{
  float sink = 0.0f;
  const int n = A.n;
  const int nv = (n / 8) * 8;                 // padded tail handled scalar

  const __m256 vFreq  = _mm256_set1_ps(frequency);
  const __m256 vPoE   = _mm256_set1_ps(sPoE);
  const __m256 vDt    = _mm256_set1_ps(sDt);
  const __m256 vAmpE  = _mm256_set1_ps(sAmplE);
  const __m256 vAmp   = _mm256_set1_ps(amplitude);
  const __m256 vInvR  = _mm256_set1_ps(1.0f / rate);
  const __m256 vPhD   = _mm256_set1_ps(phaseDiff);
  const __m256 vCut   = _mm256_set1_ps(CUTOFF);
  const __m256 vCutF  = _mm256_set1_ps(CUTOFF - 1000.0f);
  const __m256 vK1000 = _mm256_set1_ps(1.0f / 1000.0f);
  const __m256 vOne   = _mm256_set1_ps(1.0f);
  const __m256 vZero  = _mm256_setzero_ps();

  for (int t = 0; t < samples; ++t) {
    __m256 vacc = _mm256_setzero_ps();
    for (int i = 0; i < nv; i += 8) {
      __m256 pmult = _mm256_loadu_ps(&A.pmult[i]);
      __m256 p1    = _mm256_loadu_ps(&A.po1[i]);
      __m256 p2    = _mm256_loadu_ps(&A.po2[i]);
      __m256 ppo   = _mm256_fmadd_ps(_mm256_sub_ps(p2, p1), vPoE, p1);

      __m256 d     = _mm256_mul_ps(vDt, _mm256_loadu_ps(&A.dt[i]));
      __m256 pfreq = _mm256_mul_ps(_mm256_mul_ps(pmult, vFreq),
                                   _mm256_add_ps(vOne, d));

      // live = pfreq <= CUTOFF ; dead lanes contribute 0 AND keep their state
      __m256 live = _mm256_cmp_ps(pfreq, vCut, _CMP_LE_OQ);

      __m256 pos = _mm256_loadu_ps(&A.pos[i]);
      __m256 lpo = _mm256_loadu_ps(&A.lpo[i]);
      __m256 x = _mm256_add_ps(
          _mm256_fmadd_ps(pfreq, vInvR, pos),
          _mm256_add_ps(vPhD, _mm256_sub_ps(ppo, lpo)));
      x = _mm256_sub_ps(x, _mm256_round_ps(x, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC));
      x = _mm256_add_ps(x, _mm256_and_ps(_mm256_cmp_ps(x, vZero, _CMP_LT_OQ), vOne));

      __m256 newPos = _mm256_blendv_ps(pos, x, live);
      __m256 newLpo = _mm256_blendv_ps(lpo, ppo, live);
      _mm256_storeu_ps(&A.pos[i], newPos);
      _mm256_storeu_ps(&A.lpo[i], newLpo);

      __m256 fadeRaw = _mm256_mul_ps(_mm256_sub_ps(vCut, pfreq), vK1000);
      __m256 fade = _mm256_blendv_ps(fadeRaw, vOne,
                                     _mm256_cmp_ps(pfreq, vCutF, _CMP_LT_OQ));

      __m256 a1 = _mm256_loadu_ps(&A.ampl1[i]);
      __m256 a2 = _mm256_loadu_ps(&A.ampl2[i]);
      __m256 av = _mm256_fmadd_ps(_mm256_sub_ps(a2, a1), vAmpE, a1);
      __m256 pampl = _mm256_mul_ps(
          _mm256_mul_ps(vAmp, av),
          _mm256_mul_ps(_mm256_loadu_ps(&A.rolloff[i]), fade));
      pampl = _mm256_and_ps(pampl, live);     // dead lanes -> 0

      vacc = _mm256_fmadd_ps(fast_sin_turns8(newPos), pampl, vacc);
    }
    // horizontal sum (different addition order than scalar — expected)
    __m128 lo = _mm256_castps256_ps128(vacc);
    __m128 hi = _mm256_extractf128_ps(vacc, 1);
    __m128 s4 = _mm_add_ps(lo, hi);
    s4 = _mm_add_ps(s4, _mm_movehl_ps(s4, s4));
    s4 = _mm_add_ss(s4, _mm_shuffle_ps(s4, s4, 1));
    float acc = _mm_cvtss_f32(s4);

    for (int i = nv; i < n; ++i) {           // scalar tail
      float pmult = A.pmult[i];
      float ppo   = A.po1[i] + (A.po2[i] - A.po1[i]) * sPoE;
      float dd    = sDt * A.dt[i];
      float pfreq = pmult * frequency * (1.0f + dd);
      if (pfreq > CUTOFF) continue;
      float x = A.pos[i] + pfreq / rate + phaseDiff + (ppo - A.lpo[i]);
      x = x - std::truncf(x);
      if (x < 0.0f) x += 1.0f;
      A.pos[i] = x; A.lpo[i] = ppo;
      float fade = (pfreq < CUTOFF - 1000.0f) ? 1.0f : (CUTOFF - pfreq) / 1000.0f;
      acc += fast_sin_turns(A.pos[i]) * amplitude *
             (A.ampl1[i] + (A.ampl2[i] - A.ampl1[i]) * sAmplE) *
             A.rolloff[i] * fade;
    }
    sink += acc;
  }
  return sink;
}

// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
  int samples = (argc > 1) ? std::atoi(argv[1]) : 48000;

  const float rate      = 48000.0f;
  const float amplitude = 0.5f;
  const float sPoE = 0.0f, sDt = 0.002f, sAmplE = 0.0f, phaseDiff = 0.0f;

  printf("additive SIMD prototype (AVX2, 8 lanes) — %d samples/run\n", samples);
  printf("%6s %8s %9s %9s %9s %9s %8s %8s %8s\n",
         "N", "baseHz", "scalar", "sc+cast", "sc+c+rcp", "simd",
         "cast", "c+rcp", "simd");
  printf("       (ns per sample per partial)                    "
         "  (speedup vs scalar)\n");

  const int counts[] = {32, 96, 200, 512};
  for (int ci = 0; ci < 4; ++ci) {
    int n = counts[ci];
    // Base frequency chosen so ~15% of partials land past the 16 kHz cutoff,
    // matching what a real 96-partial viola note does at its top note.
    float frequency = (CUTOFF / float(n)) * 1.18f;

    Arrays A; A.build(n, frequency);

    // warm
    A.reseed(); volatile float w1 = run_scalar(A, 256, frequency, rate, amplitude, sPoE, sDt, sAmplE, phaseDiff);
    A.reseed(); volatile float w2 = run_simd  (A, 256, frequency, rate, amplitude, sPoE, sDt, sAmplE, phaseDiff);
    (void)w1; (void)w2;

    double bestS = 1e300, bestC = 1e300, bestR = 1e300, bestV = 1e300;
    float rs = 0.0f, rc = 0.0f, rr = 0.0f, rv = 0.0f;
    for (int rep = 0; rep < 9; ++rep) {
      A.reseed();
      auto t0 = std::chrono::high_resolution_clock::now();
      rs = run_scalar(A, samples, frequency, rate, amplitude, sPoE, sDt, sAmplE, phaseDiff);
      auto t1 = std::chrono::high_resolution_clock::now();
      bestS = std::min(bestS, std::chrono::duration<double, std::nano>(t1 - t0).count());

      A.reseed();
      auto tc0 = std::chrono::high_resolution_clock::now();
      rc = run_scalar_cast(A, samples, frequency, rate, amplitude, sPoE, sDt, sAmplE, phaseDiff);
      auto tc1 = std::chrono::high_resolution_clock::now();
      bestC = std::min(bestC, std::chrono::duration<double, std::nano>(tc1 - tc0).count());

      A.reseed();
      auto tr0 = std::chrono::high_resolution_clock::now();
      rr = run_scalar_cast_recip(A, samples, frequency, rate, amplitude, sPoE, sDt, sAmplE, phaseDiff);
      auto tr1 = std::chrono::high_resolution_clock::now();
      bestR = std::min(bestR, std::chrono::duration<double, std::nano>(tr1 - tr0).count());

      A.reseed();
      auto t2 = std::chrono::high_resolution_clock::now();
      rv = run_simd(A, samples, frequency, rate, amplitude, sPoE, sDt, sAmplE, phaseDiff);
      auto t3 = std::chrono::high_resolution_clock::now();
      bestV = std::min(bestV, std::chrono::duration<double, std::nano>(t3 - t2).count());
    }
    if (rs != rc) printf("  !! cast variant diverges from scalar: %.6f vs %.6f\n", rs, rc);
    if (rs != rr) printf("  .. recip variant differs (expected, not bit-exact): %.6f vs %.6f\n", rs, rr);

    // accuracy: per-sample output comparison over a short window
    A.reseed();
    std::vector<float> outS(2048), outV(2048);
    for (int t = 0; t < 2048; ++t)
      outS[t] = run_scalar(A, 1, frequency, rate, amplitude, sPoE, sDt, sAmplE, phaseDiff);
    A.reseed();
    for (int t = 0; t < 2048; ++t)
      outV[t] = run_simd(A, 1, frequency, rate, amplitude, sPoE, sDt, sAmplE, phaseDiff);
    float maxd = 0.0f, maxa = 0.0f;
    for (int t = 0; t < 2048; ++t) {
      maxd = std::max(maxd, std::fabs(outS[t] - outV[t]));
      maxa = std::max(maxa, std::fabs(outS[t]));
    }

    double perS = bestS / (double(samples) * n);
    double perC = bestC / (double(samples) * n);
    double perR = bestR / (double(samples) * n);
    double perV = bestV / (double(samples) * n);
    printf("%6d %8.1f %9.2f %9.2f %9.2f %9.2f %7.2fx %7.2fx %7.2fx\n",
           n, frequency, perS, perC, perR, perV,
           perS / perC, perS / perR, perS / perV);
    printf("       simd vs scalar: max abs diff %.3e (rel %.2e over %d samples)\n",
           maxd, maxa > 0 ? maxd / maxa : 0.0, 2048);
    (void)rv;
  }
  return 0;
}
