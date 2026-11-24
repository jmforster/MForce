#pragma once
#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace mforce::ui {

struct Session;

// The waveform strips of the last render, as plain data: how they are looked
// at, which there are, the least and greatest sample of each pixel column,
// and what each gesture does to the view. A view reports what the hand did
// (a notch of the wheel, a drag of so many pixels, a key) and draws; every
// amount is worked out here.

// A strip's left margin, holding its label and its peaks; the rest of its
// width is its plot, the width every amount below is reckoned in.
constexpr int kStripMargin = 45;
// The zoom the arrows reach, in samples per pixel column.
constexpr int kMostZoom = 4096;

// How the strips are looked at. Not part of the patch; kept between renders.
struct WaveView {
  int  zoom{1};          // samples per pixel column: the arrows keep 1 to 4096, Fit may go above
  int  scroll{0};        // the first sample shown
  int  columns{1};       // 1 to 4
  bool envelopes{true};  // envelope strips shown
  bool fitted{false};    // the first fit has been made
};

// A strip popped out into a window of its own. Its number is never given to
// another in the session; its window is named by it.
struct PopOut {
  int         number{0};
  std::string node;      // empty for the Output
  WaveView    view;      // starts as the pane's
};

// One strip to draw: its words, whose signal, and where its samples are.
// `samples` is a view into the session's take (C#'s ReadOnlySpan<float>): it
// is good until the take is replaced, so a strip is made each frame and
// never kept.
struct Strip {
  std::string label;                 // "Output", or the node's id
  std::string node;                  // empty for the Output
  bool envelope{false};              // a node of the Envelope family
  std::span<const float> samples;    // empty for a node the take did not capture
};

// The strips the pane draws, in order: the Output (the take as one line),
// then each node the take captured that is not a Note's, in the patch's node
// order, envelopes left out while session.waves.envelopes is off. A node the
// patch no longer has gets none. With no take: the Output alone, with no
// samples. A plain sound's, Chords' and Drums' takes show the Output alone.
std::vector<Strip> strips_of(const Session& s);
// The strip a pop-out shows: `node`'s, an envelope whether or not the pane
// shows envelopes; the Output for an empty name; a node that is gone, or was
// not captured, a strip with no samples.
Strip strip_of(const Session& s, const std::string& node);

// The least and the greatest of a run of samples.
struct Extent { float lo{0}, hi{0}; };

// The tables `extents` reads from: for each strip of one take, the least and
// greatest of every 64 samples and of all of them. A table is made the first
// time a strip is asked for, and every table is dropped when another take is
// asked for. It holds numbers only: no view into the take. A view owns one.
class ExtentCache {
public:
  static constexpr int kBucket = 64;
  struct Table {
    std::size_t count{0};            // the samples it was made from
    Extent all;                      // of every sample: the peaks
    std::vector<Extent> buckets;     // of samples [64k, 64k + 64)
  };
  // The table of the strip of `node` in take number `take` (session.takes),
  // made from `samples` when it has none or was made from another count.
  const Table& table(int take, const std::string& node, std::span<const float> samples);

private:
  int take_{-1};
  std::map<std::string, Table> tables_;
};

// Pixel columns of `strip`, take number `take`, from sample `scroll`, `zoom`
// samples each, into `into`: column c holds the least and the greatest of
// exactly samples [scroll + c * zoom, scroll + (c + 1) * zoom), the last one
// fewer where the take ends. Returns how many columns hold samples; the
// rest of `into` is left as it was.
int extents(const Strip& strip, int take, int zoom, int scroll, std::span<Extent> into, ExtentCache& cache);
// The strip's peaks: lo the least sample or 0, hi the greatest or 0.
Extent peaks(const Strip& strip, int take, ExtentCache& cache);

// --- the view, over a take of `total` samples and a plot `plotWidth` wide ---
void zoom_in(WaveView& v);                         // halve, down to 1
void zoom_out(WaveView& v);                        // double, up to 4096; nothing from 4096 up
// The whole take in one plot's width: total / plotWidth samples a column, at
// least 1, with no upper limit; the scroll at the start. Nothing for an empty take.
void fit(WaveView& v, int total, int plotWidth);
// Fit, once in the view's life: when it has not been made and there is a
// take and a width.
void first_fit(WaveView& v, int total, int plotWidth);
// The furthest the scroll goes: max(0, total - plotWidth * zoom).
int  max_scroll(const WaveView& v, int total, int plotWidth);
void scroll_by(WaveView& v, int samples, int total, int plotWidth);   // kept within 0 and max_scroll
void scroll_to(WaveView& v, int sample, int total, int plotWidth);
// The wheel turned `notches` (a part of one too; positive away from the
// hand): a notch scrolls a tenth of the plot's samples, away to the left;
// with Ctrl, up halves the zoom and down doubles it.
void wheel(WaveView& v, float notches, bool ctrl, int total, int plotWidth);
// A left drag of `pixels` across: the samples under the mouse follow it.
void drag(WaveView& v, float pixels, int total, int plotWidth);
// The strips' keys while the mouse is over them: Left and Right scroll a
// tenth of a page (a page with Shift), Home and End go to the ends, ZoomIn
// (= and keypad +) halves the zoom, ZoomOut (- and keypad -) doubles it, Fit (0).
enum class WaveKey { Left, Right, Home, End, ZoomIn, ZoomOut, Fit };
void wave_key(WaveView& v, WaveKey key, bool shift, int total, int plotWidth);
void set_columns(WaveView& v, int columns);        // kept within 1 to 4
void show_envelopes(WaveView& v, bool shown);

// Pops `node`'s strip out (empty: the Output), its view starting as the
// pane's; the same strip may be popped out more than once.
void open_pop_out(Session& s, const std::string& node);
void close_pop_out(Session& s, int number);

}  // namespace mforce::ui
