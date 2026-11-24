#pragma once
#include "mforce/contract/performance.h"
#include "mforce/core/spsc_queue.h"
#include "mforce/patch/patch.h"
#include "mforce/render/fault_report.h"
#include "mforce/render/pitched_instrument.h"
#include "mforce/render/ring_out.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace mforce {

// One instrument played live behind one function, fill().
//
// Two threads use it. The control thread (one thread: note_on, note_off,
// stop, the setters, sounding, sounding_notes, collect_faults) owns the idle
// voices and its records of the voices it has handed over; it starts a voice
// before sending it. The audio thread (fill) owns the voices it was sent and
// its record of what each is doing, and gives each voice back when it is
// over. So a voice is touched by one thread at a time, and ownership passes
// with a message. Both threads touch the two queues, the volume, the
// instrument's played state (wheel and pressure: atomics the control thread
// writes and the audio thread reads), and each node's fault byte: an atomic
// the audio thread sets and collect_faults exchanges, the one thing of a
// sounding voice the control thread touches.
//
// A note marked `hold` on an instrument that sustains joins the one legato
// line, or opens it; on one that does not, it holds a voice of its own until
// its note_off. A voice that cannot hold (no envelope to gate) plays the
// note's length either way.
//
// A pin's constant (set_value) is written into a voice by whichever thread
// holds it: the audio thread writes a Set message into each voice it holds,
// and the control thread writes every value set so far into a voice just
// before it starts it. So each voice is still written by one thread at a
// time, and a voice has every value whichever thread held it when the value
// was set.
class LivePlayer {
public:
  using NoteId = std::uint32_t;

  // Sounds at most `voices` notes at once, or the patch's polyphony when that
  // is larger, and builds twice that many voices: the spares let a new note
  // start while a stolen one fades. With `peakGuard` false the output is
  // scaled by the volume and not clipped: a host that sums several sounds
  // clips the sum itself.
  LivePlayer(const Patch& patch, int voices, bool peakGuard = true);

  // --- control thread ---
  // The note at the top of the next buffer, or carried onto the open line (a
  // carried note that names no onset takes the instrument's "slur" when it
  // has one). A held note that opens the line first fades every other voice
  // handed over to nothing in 3 ms. 0 when it is not sent: no idle voice, the
  // queue has no room for everything the note needs, or a key-up or a stop
  // is still waiting to be sent. An id is never 0.
  NoteId note_on(const PerformedNote& note);
  // Releases the note `id` began: the line's current note, or a voice held
  // on its own. An id that names nothing sounding and held (a struck note, a
  // voice stolen or given back, an earlier note of the line, 0) does nothing.
  // A key-up the queue cannot take now is kept and sent by a later control
  // call: it is never lost.
  void note_off(NoteId id);
  // Every voice handed over fades out over the fade a stolen voice gets and
  // comes back; the line closes and the key-ups waiting are dropped. The
  // messages before it are played first. A stop the queue cannot take now is
  // kept and sent by a later control call.
  void stop();
  void set_wheel(float v);
  void set_pressure(float v);
  void set_volume(float v);
  // The constant of `pin` on `node`, written in every voice: the voices that
  // are sounding have it within one buffer, the others when they next start.
  // The sound follows for a node that reads its pin's number as it runs (on
  // the sounding note) or as a note starts (from the next note); render_tests'
  // census holds every node type and pin the measured patches use to that.
  // False when the player cannot follow it, and nothing changes: the voices
  // have no constant of that node and pin (a pin a wire feeds, a pin with no
  // number, a name that is not there, a node a MultiplexSource's template
  // reaches); the caller builds a new player then. A Set the queue cannot
  // take now is kept and sent by a later control call: a value is never
  // lost. Allocates nothing.
  bool set_value(std::string_view node, std::string_view pin, float v);
  int  sample_rate() const;
  // The voices handed to the audio thread and not yet given back.
  int  sounding();
  // The notes (rounded) of the voices handed over and not yet given back,
  // lowest first: as many as fit in `into`, the lowest of them. Returns how
  // many it wrote. Allocates nothing.
  int  sounding_notes(std::span<int> into);
  // What the nodes of every voice reported since the last call, a node's
  // clones as one entry, worded and ordered as PitchedInstrument's.
  void collect_faults(std::vector<FaultReport>& out);

  // --- audio thread ---
  // `frames` samples of the instrument's mono output. Never blocks,
  // allocates, frees, prints or throws.
  void fill(float* mono, int frames) noexcept;

private:
  // A message from the control thread to the audio thread.
  struct Message {
    enum class Kind : std::uint8_t { Start, Carry, Release, Fade, Set, Stop };
    Kind  kind{Kind::Start};
    int   voice{-1};
    float gain{1.0f};          // Start: the note's velocity
    int   bodySamples{0};      // Start: the note's length in samples
    bool  held{false};         // Start: the voice holds until released
    Voice::Note note{};        // Carry: the next note (no curve)
    bool  hold{false};         // Carry: the next note holds
    float fromFreq{0.0f};      // Carry: the frequency glided from
    int   fadeSamples{0};      // Fade: the fade's length
    int   constant{-1};        // Set: the index into every voice's constants()
    float value{0.0f};         // Set: the constant's new number
  };
  // A voice the audio thread gives back.
  struct Done { int voice{-1}; };

  // The audio thread's record of a sounding voice.
  struct Sounding {
    int   voice{-1};
    float gain{1.0f};
    RingOut ring;
    int   played{0};           // samples since the voice started
    int   noteSamples{0};      // samples since the current note began
    int   releaseAt{-1};       // release when the current note has played this many
    int   fadeLeft{-1};        // samples of the fade still to play; -1 when not fading
    int   fadeLength{1};       // the fade's whole length
    bool  over{false};
  };
  // The control thread's record of a voice.
  struct Handed {
    bool     out{false};       // handed to the audio thread
    NoteId   id{0};            // the note it plays now
    int      note{0};          // that note's number, rounded
    std::uint64_t startedAt{0};
    std::uint64_t releasedAt{0};   // when no key held it any more
    bool     released{false};      // no key holds it
    bool     fading{false};
  };
  // The open held line, on the control side.
  struct Line {
    bool   open{false};
    int    voice{-1};
    NoteId id{0};              // its current note
    float  lastFreq{0.0f};
  };

  static constexpr std::size_t kQueueSize = 256;

  // The voice `v` of the instrument's pool: the player is the instrument's
  // other driver.
  Voice& voice(int v);

  // Construction: every voice lists the same constants in the same order.
  void check_constants_agree();
  // Control thread.
  void catch_up();
  bool waiting() const;
  int  constant_index(std::string_view node, std::string_view pin);
  Message set_message(int k) const;
  void send_unsent();
  void send_releases();
  void write_kept(Voice& v);
  Message release_message(int v) const;
  Message fade_message(int v, int samples) const;
  NoteId take_id();
  int  idle_voice() const;
  int  steal_victim() const;
  int  to_cut() const;
  NoteId carry(const PerformedNote& note, const Voice::Note& n);
  NoteId start_fresh(const PerformedNote& note, const Voice::Note& n);
  void fade_out(int v, int samples);
  // Audio thread.
  void apply(const Message& m) noexcept;
  Sounding* find(int v) noexcept;
  void release(Sounding& s, int lastNoteSamples) noexcept;
  void render(Sounding& s, float* mono, int frames) noexcept;
  void give_back() noexcept;

  // Set at construction. The instrument's voices pass between the threads
  // by hand-over; its played state is atomics; nothing else of it changes
  // while the player runs.
  std::unique_ptr<PitchedInstrument> instrument_;
  int sampleRate_{0};
  int voices_{0};
  int fadeSamples_{0};       // a stolen voice's fade, and stop's
  int cutSamples_{0};        // the fade of what rings when a line opens
  float glideSec_{0.0f};
  bool sustains_{false};
  int slurOnset_{0};
  bool peakGuard_{true};

  // Both threads.
  SpscQueue<Message, kQueueSize> toAudio_;
  SpscQueue<Done, kQueueSize>    toControl_;
  std::atomic<float>             volume_{1.0f};

  // Control thread only.
  std::vector<Handed> handed_;   // one per voice of the pool
  Line line_;
  // By voice: its Release waits for room in the queue.
  std::vector<std::uint8_t> releaseWaits_;
  int waitingReleases_{0};
  bool stopWaits_{false};
  int lastStarted_{-1};
  NoteId nextId_{1};
  std::uint64_t nextOrder_{0};
  // By index into a voice's constants(), sized at construction: the latest
  // value set; whether one was set; whether its Set waits for room.
  std::vector<float> kept_;
  std::vector<std::uint8_t> isKept_;
  std::vector<std::uint8_t> unsent_;
  int unsentCount_{0};

  // Audio thread only.
  std::vector<Sounding> sounding_;   // capacity for every voice, reserved at construction
};

} // namespace mforce
