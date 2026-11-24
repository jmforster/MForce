// The strips as plain data: each pixel column's least and greatest against
// the plain loop, the cache, Fit and the first fit, the scroll's limits, each
// gesture, which strips a take shows, and the pop-outs.
#include "mforce/testing/check.h"
#include "transport_fixture.h"
#include "mforce/ui_control/building.h"
#include "mforce/ui_control/waveforms.h"
#include <cstdio>
#include <random>
#include <string>
#include <vector>

using namespace transport_fixture;

namespace {

constexpr int kPlot = 800;

std::vector<float> noise(std::size_t n, unsigned seed) {
  std::mt19937 rng(seed);
  std::uniform_real_distribution<float> d(-1.0f, 1.0f);
  std::vector<float> v(n);
  for (float& x : v) x = d(rng);
  return v;
}

// The columns the plain loop gives, as `extents` should.
std::vector<Extent> plain_columns(const std::vector<float>& v, int zoom, int scroll, int width) {
  std::vector<Extent> out;
  for (long long c = 0; c < width; ++c) {
    const long long from = scroll + c * zoom;
    if (from >= (long long)v.size()) break;
    const long long to = std::min<long long>(from + zoom, (long long)v.size());
    Extent e{v[size_t(from)], v[size_t(from)]};
    for (long long i = from; i < to; ++i) e = Extent{std::min(e.lo, v[size_t(i)]), std::max(e.hi, v[size_t(i)])};
    out.push_back(e);
  }
  return out;
}

// Every column of `extents` equals the plain loop's, and as many hold samples.
bool same_as_plain(const std::vector<float>& v, int take, int zoom, int scroll, ExtentCache& cache) {
  std::vector<Extent> into(kPlot);
  const Strip strip{"n", "n", false, v};
  const int filled = extents(strip, take, zoom, scroll, into, cache);
  const auto want = plain_columns(v, zoom, scroll, kPlot);
  if (filled != int(want.size())) return false;
  for (int c = 0; c < filled; ++c)
    if (into[size_t(c)].lo != want[size_t(c)].lo || into[size_t(c)].hi != want[size_t(c)].hi) return false;
  return true;
}

std::vector<std::string> labels(const std::vector<Strip>& strips) {
  std::vector<std::string> out;
  for (const auto& s : strips) out.push_back(s.label);
  return out;
}

}  // namespace

// At every zoom the arrows reach and two Fit leaves (one above 4096), from a
// scroll that is no multiple of 64, on a take longer than a plot and one
// shorter: exactly the plain loop's numbers; again from the cache; and after
// a new take of the same length, never the old take's.
void run_extents_tests() {
  std::printf("strips: each column's least and greatest, exactly, from the cache too, never a take before\n");
  const std::vector<float> longer = noise(3'000'000, 1), shorter = noise(300, 2);
  std::vector<int> zooms;
  for (int z = 1; z <= kMostZoom; z *= 2) zooms.push_back(z);
  zooms.push_back(1500);
  zooms.push_back(5000);
  ExtentCache cache;
  for (int z : zooms)
    for (int scroll : {0, 37, 64 * 5 + 13}) {
      CHECK(same_as_plain(longer, 1, z, scroll, cache));
      CHECK(same_as_plain(longer, 1, z, scroll, cache));   // served from the table made the first time
    }
  ExtentCache small;
  for (int z : zooms) CHECK(same_as_plain(shorter, 7, z, 37, small));
  const std::vector<float> again = noise(longer.size(), 3);                 // the same length, other numbers
  ExtentCache c2;
  CHECK(same_as_plain(longer, 1, 64, 37, c2));
  CHECK(same_as_plain(again, 2, 64, 37, c2));
  CHECK(same_as_plain(again, 2, 4096, 0, c2));
  const Strip strip{"n", "n", false, again};
  const Extent p = peaks(strip, 2, c2);
  CHECK(p.lo <= 0.0f && p.hi >= 0.0f && p.lo == *std::min_element(again.begin(), again.end()));
  const std::vector<float> positive(100, 0.5f);
  const Extent up = peaks(Strip{"u", "u", false, positive}, 3, c2);
  CHECK(up.lo == 0.0f && up.hi == 0.5f);                                    // the lower peak is the least or 0
  std::vector<Extent> into(kPlot);
  CHECK(extents(Strip{"e", "e", false, {}}, 3, 1, 0, into, c2) == 0);
}

// Fit, the first fit once, the scroll's limits at a column's plot and a
// quarter of it, the zoom's limits, and each gesture's amount and direction.
void run_wave_view_tests() {
  std::printf("strips: fit, the first fit once, the scroll's limits, the zoom's, each gesture\n");
  const int total = 100'000;
  for (int width : {kPlot, kPlot / 4}) {
    WaveView v;
    first_fit(v, 0, width);
    CHECK(!v.fitted && v.zoom == 1);
    first_fit(v, total, width);
    CHECK(v.fitted && v.zoom == total / width && v.scroll == 0);
    v.zoom = 8;
    first_fit(v, total, width);
    CHECK(v.zoom == 8);                                                     // once only
    CHECK(max_scroll(v, total, width) == total - width * 8);
    scroll_by(v, 1'000'000, total, width);
    CHECK(v.scroll == total - width * 8);
    scroll_by(v, -2'000'000, total, width);
    CHECK(v.scroll == 0);
    v.zoom = 4096;
    CHECK(max_scroll(v, total, width) == 0);
    scroll_to(v, 500, total, width);
    CHECK(v.scroll == 0);                                                   // nothing to scroll: 0, never 1
  }
  WaveView v;
  fit(v, 10'000'000, 1000);
  CHECK(v.zoom == 10'000);                                                  // Fit has no upper limit
  zoom_out(v);
  CHECK(v.zoom == 10'000);                                                  // the arrow does nothing above 4096
  zoom_in(v);
  CHECK(v.zoom == 5'000);
  v.zoom = 2048;
  zoom_out(v);
  zoom_out(v);
  CHECK(v.zoom == 4096);
  v.zoom = 1;
  zoom_in(v);
  CHECK(v.zoom == 1);
  v = WaveView{};
  v.zoom = 10;
  scroll_to(v, 50'000, total, kPlot);
  wheel(v, 1.0f, false, total, kPlot);
  CHECK(v.scroll == 50'000 - kPlot * 10 / 10);                              // a notch up: a tenth of the plot to the left
  wheel(v, -0.5f, false, total, kPlot);
  CHECK(v.scroll == 50'000 - 400);                                          // half a notch: half the amount
  wheel(v, 1.0f, true, total, kPlot);
  CHECK(v.zoom == 5);
  wheel(v, -1.0f, true, total, kPlot);
  CHECK(v.zoom == 10);
  const int before = v.scroll;
  drag(v, 30.0f, total, kPlot);
  CHECK(v.scroll == before - 300);                                          // the samples follow the mouse
  wave_key(v, WaveKey::Right, false, total, kPlot);
  CHECK(v.scroll == before - 300 + 800);
  wave_key(v, WaveKey::Left, true, total, kPlot);
  CHECK(v.scroll == before - 300 + 800 - 8000);
  wave_key(v, WaveKey::End, false, total, kPlot);
  CHECK(v.scroll == total - 8000);
  wave_key(v, WaveKey::Home, false, total, kPlot);
  CHECK(v.scroll == 0);
  wave_key(v, WaveKey::ZoomIn, false, total, kPlot);
  CHECK(v.zoom == 5);
  wave_key(v, WaveKey::ZoomOut, false, total, kPlot);
  CHECK(v.zoom == 10);
  wave_key(v, WaveKey::Fit, false, total, kPlot);
  CHECK(v.zoom == total / kPlot && v.scroll == 0);
  set_columns(v, 9);
  CHECK(v.columns == 4);
  set_columns(v, 0);
  CHECK(v.columns == 1);
  show_envelopes(v, false);
  CHECK(!v.envelopes);
}

// A Note take of oboe1: the Output, then its nodes in the patch's order, the
// Notes' left out, envelopes with Envelopes on and off; Chords, a plain sound
// and no take show the Output alone; a pop-out's strip of a node that went;
// pop-out numbers never reused; New and Open close them.
void run_strips_of_tests() {
  std::printf("strips: which a take shows and in what order; pop-outs\n");
  Session s;
  CHECK(labels(strips_of(s)) == std::vector<std::string>{"Output"} && strips_of(s)[0].samples.empty());
  open_file(s, kOboe);
  generate(s);
  std::vector<std::string> want{"Output"};
  int envelopes = 0;
  for (const auto& n : s.open.patch.graph.nodes) {
    if (n.type == "PerformNode") continue;
    want.push_back(n.id);
    if (n.type == "Envelope" || n.type == "Vibrato") ++envelopes;
  }
  const auto strips = strips_of(s);
  CHECK(labels(strips) == want && want.size() == 19 && envelopes == 4);
  CHECK(strips[0].samples.size() == size_t(s.take.frames) && strips[1].samples.size() == size_t(s.take.frames));
  CHECK(strips[0].samples[100] == 0.5f * (s.take.stereo[200] + s.take.stereo[201]));
  show_envelopes(s.waves, false);
  CHECK(strips_of(s).size() == 15);
  CHECK(strip_of(s, "Ampl_env").envelope && !strip_of(s, "Ampl_env").samples.empty());   // a pop-out shows it anyway
  show_envelopes(s.waves, true);
  open_pop_out(s, "Damp_lpf");
  open_pop_out(s, "Damp_lpf");
  open_pop_out(s, "");
  CHECK(s.popOuts.size() == 3 && s.popOuts[0].number == 1 && s.popOuts[2].number == 3 && s.popOuts[2].node.empty());
  close_pop_out(s, 2);
  open_pop_out(s, "Reverb");
  CHECK(s.popOuts.size() == 3 && s.popOuts[1].number == 3 && s.popOuts.back().number == 4);
  s.selected = "Damp_lpf";
  delete_these(s, {}, {"Damp_lpf"});
  const Strip gone = strip_of(s, "Damp_lpf");
  CHECK(gone.samples.empty() && gone.label == "Damp_lpf");
  CHECK(strips_of(s).size() == 18);                                         // a node the patch lost has no strip
  s.transport.mode = Transport::Mode::Chords;
  s.transport.chords = "C_q";
  generate(s);
  CHECK(!s.take.stereo.empty() && labels(strips_of(s)) == std::vector<std::string>{"Output"});
  open_file(s, kPlain);
  CHECK(s.popOuts.empty());
  generate(s);
  CHECK(!strips_of(s)[0].samples.empty() && strips_of(s).size() == 1);
  open_pop_out(s, "");
  new_file(s);
  CHECK(s.popOuts.empty());
  open_pop_out(s, "");
  CHECK(s.popOuts.back().number == 6);                                      // never given again
}

// Every place the take is replaced or cleared counts.
void run_take_count_tests() {
  std::printf("strips: every take replaced or cleared is counted\n");
  Session s;
  const int start = s.takes;
  open_file(s, kOboe);
  CHECK(s.takes > start);
  int before = s.takes;
  generate(s);
  CHECK(s.takes > before && !s.take.mono.empty());
  before = s.takes;
  new_file(s);
  CHECK(s.takes > before && s.take.mono.empty());
  before = s.takes;
  generate(s);                                                              // refused: nothing is wired to the Output
  CHECK(s.takes > before && s.take.stereo.empty() && s.take.mono.empty());
}
