// STK Mesh2D reference renderer — ground truth for the MForce port.
// Rectilinear 2D waveguide mesh (Van Duyne & Smith 1993).
//
// Two engines render here:
//   * stk::Mesh2D itself, linked from the sibling checkout, unmodified.
//     This is the real ground truth — but STK caps the mesh at NXMAX =
//     NYMAX = 12 and hard-codes the output tap at the far corner, so it can
//     only serve the small mesh with a corner output.
//   * MeshBig, a transcription of Mesh2D::tick0/tick1 that differs from STK
//     in exactly two ways: the array cap is 64 (Chafe's 25x6 plate does not
//     fit in 12) and the output tap indices come from a settable output
//     junction, reducing to STK's literal expression at the far corner.
//     It uses stk::OnePole for the edge filters, so the filtering is STK's
//     own code, not a second transcription.
// main() runs an identity check first: MeshBig(12,12) with a corner output
// must reproduce stk::Mesh2D(12,12) sample for sample. That certifies the
// transcription, after which MeshBig's big-mesh and moved-tap renders are
// usable as ground truth.
//
// Excitation is injected via inputTick(), NOT noteOn/strike: the port has to
// be able to inject the identical signal, and STK's own strike is a corner
// impulse the graph cannot express. The standard strike is a 1 ms raised
// cosine of amplitude 0.5, computed here with the same float arithmetic the
// MForce Envelope uses for a pair of Sine ramps (see gen_stk_mesh2d.py) so
// the two sides are driven by bit-identical samples.
//
// argv[1] = output dir, argv[2] = sample rate (default 48000).

#include "Mesh2D.h"
#include "OnePole.h"
#include "Stk.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <string>
#include <vector>

using namespace stk;

static double kSampleRate = 48000.0;
static const double kSlotSec = 6.0;
static const double kBurstSec = 0.001;
static const float kBurstAmp = 0.5f;

// ---------------------------------------------------------------------------
// MeshBig — stk::Mesh2D with a 64 cap and a movable output tap.
// ---------------------------------------------------------------------------
namespace {

const int NMAX = 64;

class MeshBig {
 public:
  MeshBig(int nX, int nY) : NX_(nX), NY_(nY) {
    const StkFloat pole = 0.05;
    for (int i = 0; i < NMAX; i++) {
      filterY_[i].setPole(pole); filterY_[i].setGain(0.99);
      filterX_[i].setPole(pole); filterX_[i].setGain(0.99);
    }
    clear();
  }

  void clear() {
    for (int x = 0; x < NMAX; x++)
      for (int y = 0; y < NMAX; y++) {
        v_[x][y] = 0;
        vxp_[x][y] = vxm_[x][y] = vyp_[x][y] = vym_[x][y] = 0;
        vxp1_[x][y] = vxm1_[x][y] = vyp1_[x][y] = vym1_[x][y] = 0;
      }
    for (int i = 0; i < NMAX; i++) { filterX_[i].clear(); filterY_[i].clear(); }
    counter_ = 0;
  }

  void setDecay(StkFloat d) {
    for (int i = 0; i < NMAX; i++) { filterX_[i].setGain(d); filterY_[i].setGain(d); }
  }

  // STK's truncation, verbatim.
  void setInputPosition(StkFloat xf, StkFloat yf) {
    xIn_ = (int)(xf * (NX_ - 1));
    yIn_ = (int)(yf * (NY_ - 1));
  }
  void setOutputPosition(StkFloat xf, StkFloat yf) {
    xOut_ = (int)(xf * (NX_ - 1));
    yOut_ = (int)(yf * (NY_ - 1));
  }

  StkFloat inputTick(StkFloat input) {
    StkFloat out;
    if (counter_ & 1) {
      vxp1_[xIn_][yIn_] += input;
      vyp1_[xIn_][yIn_] += input;
      out = tick1();
    } else {
      vxp_[xIn_][yIn_] += input;
      vyp_[xIn_][yIn_] += input;
      out = tick0();
    }
    counter_++;
    return out;
  }

 private:
  static constexpr StkFloat VSCALE = 0.5;

  StkFloat tick0() {
    int x, y;
    for (x = 0; x < NX_ - 1; x++)
      for (y = 0; y < NY_ - 1; y++)
        v_[x][y] = (vxp_[x][y] + vxm_[x + 1][y] +
                    vyp_[x][y] + vym_[x][y + 1]) * VSCALE;

    for (x = 0; x < NX_ - 1; x++) {
      for (y = 0; y < NY_ - 1; y++) {
        StkFloat vxy = v_[x][y];
        vxp1_[x + 1][y] = vxy - vxm_[x + 1][y];
        vyp1_[x][y + 1] = vxy - vym_[x][y + 1];
        vxm1_[x][y] = vxy - vxp_[x][y];
        vym1_[x][y] = vxy - vyp_[x][y];
      }
    }

    for (y = 0; y < NY_ - 1; y++) {
      vxp1_[0][y] = filterY_[y].tick(vxm_[0][y]);
      vxm1_[NX_ - 1][y] = vxp_[NX_ - 1][y];
    }
    for (x = 0; x < NX_ - 1; x++) {
      vyp1_[x][0] = filterX_[x].tick(vym_[x][0]);
      vym1_[x][NY_ - 1] = vyp_[x][NY_ - 1];
    }

    return vxp_[xOut_][yOut_ > 0 ? yOut_ - 1 : 0] +
           vyp_[xOut_ > 0 ? xOut_ - 1 : 0][yOut_];
  }

  StkFloat tick1() {
    int x, y;
    for (x = 0; x < NX_ - 1; x++)
      for (y = 0; y < NY_ - 1; y++)
        v_[x][y] = (vxp1_[x][y] + vxm1_[x + 1][y] +
                    vyp1_[x][y] + vym1_[x][y + 1]) * VSCALE;

    for (x = 0; x < NX_ - 1; x++) {
      for (y = 0; y < NY_ - 1; y++) {
        StkFloat vxy = v_[x][y];
        vxp_[x + 1][y] = vxy - vxm1_[x + 1][y];
        vyp_[x][y + 1] = vxy - vym1_[x][y + 1];
        vxm_[x][y] = vxy - vxp1_[x][y];
        vym_[x][y] = vxy - vyp1_[x][y];
      }
    }

    for (y = 0; y < NY_ - 1; y++) {
      vxp_[0][y] = filterY_[y].tick(vxm1_[0][y]);
      vxm_[NX_ - 1][y] = vxp1_[NX_ - 1][y];
    }
    for (x = 0; x < NX_ - 1; x++) {
      vyp_[x][0] = filterX_[x].tick(vym1_[x][0]);
      vym_[x][NY_ - 1] = vyp1_[x][NY_ - 1];
    }

    return vxp1_[xOut_][yOut_ > 0 ? yOut_ - 1 : 0] +
           vyp1_[xOut_ > 0 ? xOut_ - 1 : 0][yOut_];
  }

  int NX_, NY_;
  int xIn_{0}, yIn_{0}, xOut_{0}, yOut_{0};
  int counter_{0};
  OnePole filterX_[NMAX], filterY_[NMAX];
  StkFloat v_[NMAX][NMAX];
  StkFloat vxp_[NMAX][NMAX], vxm_[NMAX][NMAX], vyp_[NMAX][NMAX], vym_[NMAX][NMAX];
  StkFloat vxp1_[NMAX][NMAX], vxm1_[NMAX][NMAX], vyp1_[NMAX][NMAX], vym1_[NMAX][NMAX];
};

// The MForce Envelope's Sine ramp is value = start + range*(cos((1+t)*PI)+1)/2
// evaluated in float at t = n/24; two stages (0->1 then 1->0) make the raised
// cosine. Replicated here op for op so both sides see identical samples.
float burst_sample(int n, int half) {
  constexpr float PI = 3.14159265358979323846f;
  if (n < 0 || n >= 2 * half) return 0.0f;
  const int stage = n < half ? 0 : 1;
  const float pos = float(n - stage * half) / float(half);
  float raw;
  if (pos <= 0.0f) raw = stage == 0 ? 0.0f : 1.0f;
  else if (stage == 0) raw = (std::cos((1.0f + pos) * PI) + 1.0f) * 0.5f;
  else raw = 1.0f - (std::cos((1.0f + pos) * PI) + 1.0f) * 0.5f;
  return kBurstAmp * raw;
}

bool write_wav16_mono(const std::string& path, const std::vector<double>& samples) {
  FILE* f = fopen(path.c_str(), "wb");
  if (!f) return false;
  uint32_t n = (uint32_t)samples.size();
  uint32_t dataBytes = n * 2, sr = (uint32_t)kSampleRate;
  uint32_t byteRate = sr * 2, riffSize = 36 + dataBytes, fmtSize = 16;
  uint16_t fmt = 1, ch = 1, block = 2, bits = 16;
  fwrite("RIFF", 1, 4, f); fwrite(&riffSize, 4, 1, f);
  fwrite("WAVEfmt ", 1, 8, f);
  fwrite(&fmtSize, 4, 1, f); fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f);
  fwrite(&sr, 4, 1, f); fwrite(&byteRate, 4, 1, f);
  fwrite(&block, 2, 1, f); fwrite(&bits, 2, 1, f);
  fwrite("data", 1, 4, f); fwrite(&dataBytes, 4, 1, f);
  for (double s : samples) {
    if (s > 1.0) s = 1.0;
    if (s < -1.0) s = -1.0;
    int16_t v = (int16_t)std::lrint(s * 32767.0);
    fwrite(&v, 2, 1, f);
  }
  fclose(f);
  return true;
}

struct Case {
  const char* name;
  int nx, ny;
  double inX, inY, outX, outY;
};

const Case kCases[] = {
  // Chafe's plate proportions (25 x 6) — beyond STK's own 12x12 cap.
  {"25x6_a", 25, 6, 0.0, 0.0, 1.0, 1.0},
  {"25x6_b", 25, 6, 0.5, 0.5, 1.0, 1.0},
  {"25x6_c", 25, 6, 0.3, 0.7, 0.6, 0.4},
  // Bar-ish 12 x 3 — inside STK's cap, so cases a/b are real stk::Mesh2D.
  {"12x3_a", 12, 3, 0.0, 0.0, 1.0, 1.0},
  {"12x3_b", 12, 3, 0.5, 0.5, 1.0, 1.0},
  {"12x3_c", 12, 3, 0.3, 0.7, 0.6, 0.4},
};

// True when stk::Mesh2D itself can render the case (mesh inside the cap and
// the output tap at the far corner, which is the only tap STK has).
bool stk_native(const Case& c) {
  return c.nx <= 12 && c.ny <= 12 && c.outX >= 1.0 && c.outY >= 1.0;
}

// MeshBig(12,12), corner tap, must equal stk::Mesh2D(12,12) exactly.
double identity_check(int frames, int half) {
  Mesh2D ref(12, 12);
  ref.clear();
  ref.setInputPosition(0.3, 0.7);
  ref.setDecay(0.99);
  MeshBig big(12, 12);
  big.setInputPosition(0.3, 0.7);
  big.setOutputPosition(1.0, 1.0);
  big.setDecay(0.99);
  double worst = 0.0;
  for (int i = 0; i < frames; ++i) {
    const double x = burst_sample(i, half);
    const double a = ref.inputTick(x);
    const double b = big.inputTick(x);
    const double d = std::fabs(a - b);
    if (d > worst) worst = d;
  }
  return worst;
}

}  // namespace

int main(int argc, char** argv) {
  std::string outdir = argc > 1 ? argv[1] : ".";
  if (argc > 2) kSampleRate = atof(argv[2]);
  Stk::setSampleRate(kSampleRate);

  const int frames = (int)(kSlotSec * kSampleRate);
  const int half = (int)(0.5 * kBurstSec * kSampleRate);   // 24 at 48 kHz

  const double idt = identity_check(48000, half);
  std::printf("identity MeshBig(12,12) vs stk::Mesh2D(12,12): max |diff| = %.3e\n",
              idt);
  if (idt != 0.0) {
    std::fprintf(stderr, "TRANSCRIPTION MISMATCH — refusing to write ground truth\n");
    return 2;
  }

  for (const Case& c : kCases) {
    std::vector<double> out;
    out.reserve(frames);
    if (stk_native(c)) {
      Mesh2D m(c.nx, c.ny);
      m.clear();
      m.setDecay(0.99);
      m.setInputPosition(c.inX, c.inY);
      for (int i = 0; i < frames; ++i) out.push_back(m.inputTick(burst_sample(i, half)));
    } else {
      MeshBig m(c.nx, c.ny);
      m.setDecay(0.99);
      m.setInputPosition(c.inX, c.inY);
      m.setOutputPosition(c.outX, c.outY);
      for (int i = 0; i < frames; ++i) out.push_back(m.inputTick(burst_sample(i, half)));
    }

    double peak = 0.0, sum = 0.0;
    for (double s : out) { peak = std::max(peak, std::fabs(s)); sum += s * s; }
    const std::string path = outdir + "/stk_mesh2d_" + c.name + ".wav";
    if (!write_wav16_mono(path, out)) {
      std::fprintf(stderr, "FAILED to write %s\n", path.c_str());
      return 1;
    }
    std::printf("wrote %s  engine=%s peak=%.4f rms=%.5f\n", path.c_str(),
                stk_native(c) ? "stk::Mesh2D" : "MeshBig",
                peak, std::sqrt(sum / out.size()));
  }
  return 0;
}
